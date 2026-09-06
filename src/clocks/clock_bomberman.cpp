/* Bomberman clock: a small hero navigates corridors between brick digits. */
#include "clocks.h"
#include "clock_globals.h"
#include "../display/display.h"
#include <math.h>

namespace {
const uint8_t glyph[10][7] = {
  {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
  {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
  {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
  {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
  {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
const int digitX[4] = {12,40,72,100};
const int digitY=16;
// Two horizontal corridors joined by five vertical passages. The middle
// waypoints are beside the digits, never through their glyphs or counters.
const int laneX[5]={6,34,64,94,122};
const int laneY[4]={8,22,34,52};
const int dx[4]={1,0,-1,0}, dy[4]={0,1,0,-1};
const uint16_t pink=0xF81F, blue=0x329F, orange=0xFC40;
const int REACH=34;
enum Phase { PATROL, APPROACH, FUSE, BLAST, BUILD, COLLECT };
Phase phase;
uint32_t lastFrame, phaseStart, fuseDuration;
float heroX,heroY;
int heroNode,bombNode,activeDigit,shown[4],target[4],facing;
int route[20],routeCount,routeIndex;
int crates[2],bonusNode;
int flameLength[4],flameDigit[4],flameCrate[4];
bool initialized;

int nx(int n) { return laneX[n%5]; }
int ny(int n) { return laneY[n/5]; }
bool occupied(int n) { return crates[0]==n || crates[1]==n; }
bool neighbors(int a,int b) {
  if(a%5==b%5 && abs(a/5-b/5)==1) return true;
  return a/5==b/5 && (a/5==0 || a/5==3) && abs(a%5-b%5)==1;
}

// Bounded BFS over the arena graph; no allocations in the frame loop.
bool findRoute(int from,int to,int* out,int& count) {
  int queue[20],parent[20],head=0,tail=0;
  for(int i=0;i<20;i++) parent[i]=-1;
  parent[from]=from; queue[tail++]=from;
  while(head<tail && parent[to]<0) {
    int n=queue[head++];
    for(int j=0;j<20;j++) if(parent[j]<0 && !occupied(j) && neighbors(n,j)) {
      parent[j]=n; queue[tail++]=j;
    }
  }
  count=0;
  if(parent[to]<0) return false;
  for(int n=to;n!=from;n=parent[n]) out[count++]=n;
  for(int i=0;i<count/2;i++) { int n=out[i]; out[i]=out[count-1-i]; out[count-1-i]=n; }
  return true;
}
bool goTo(int n) {
  routeIndex=0;
  return findRoute(heroNode,n,route,routeCount);
}
bool moveHero(float dt,float speed) {
  float remaining=dt*speed;
  while(routeIndex<routeCount) {
    int next=route[routeIndex];
    float x=nx(next)-heroX,y=ny(next)-heroY;
    float distance=fabsf(x)+fabsf(y);
    if(distance>0.01f) facing=fabsf(x)>0.01f?(x>0?0:2):(y>0?1:3);
    if(distance<=remaining) {
      heroX=nx(next); heroY=ny(next); heroNode=next;
      remaining-=distance; routeIndex++;
    } else {
      if(fabsf(x)>0.01f) heroX+=(x>0?remaining:-remaining);
      else heroY+=(y>0?remaining:-remaining);
      return false;
    }
  }
  return true;
}

int digitAt(int x,int y) {
  if(y<digitY || y>=digitY+27 || (y-digitY)%4==3) return -1;
  int r=(y-digitY)/4;
  for(int i=0;i<4;i++) {
    int local=x-digitX[i];
    if(local>=0 && local<19 && local%4!=3 && (glyph[shown[i]][r]&(16>>(local/4)))) return i;
  }
  return -1;
}
void traceFlames(int node,int* lengths,int* digits,int* boxes) {
  for(int d=0;d<4;d++) {
    lengths[d]=0; digits[d]=-1; boxes[d]=-1;
    for(int s=1;s<=REACH;s++) {
      int x=nx(node)+dx[d]*s,y=ny(node)+dy[d]*s;
      if(x<2 || x>125 || y<3 || y>59) break;
      lengths[d]=s;
      digits[d]=digitAt(x,y);
      for(int c=0;c<2;c++) if(crates[c]>=0 && abs(x-nx(crates[c]))<=3 && abs(y-ny(crates[c]))<=3) boxes[d]=c;
      if(digits[d]>=0 || boxes[d]>=0) break;
    }
  }
}
bool inFlames(int node,int origin,const int* lengths) {
  int x=nx(node)-nx(origin),y=ny(node)-ny(origin);
  for(int d=0;d<4;d++) {
    int along=x*dx[d]+y*dy[d], across=x*dy[d]-y*dx[d];
    // Include the sprite's extent, not just its centre.
    if(along>=-4 && along<=lengths[d]+4 && abs(across)<=5) return true;
  }
  return false;
}
int escapeNode(int origin,const int* lengths) {
  int best=-1,bestDistance=10000;
  for(int n=0;n<20;n++) {
    if(occupied(n) || inFlames(n,origin,lengths)) continue;
    int path[20],count;
    if(!findRoute(origin,n,path,count)) continue;
    int distance=0,prev=origin;
    for(int j=0;j<count;j++) { distance+=abs(nx(path[j])-nx(prev))+abs(ny(path[j])-ny(prev)); prev=path[j]; }
    if(distance<bestDistance) { best=n; bestDistance=distance; }
  }
  return best;
}

void respawnCrates() {
  for(int c=0;c<2;c++) if(crates[c]<0) {
    int options[10],count=0;
    for(int n=5;n<15;n++) if(n!=heroNode && n!=bonusNode && !occupied(n)) options[count++]=n;
    if(count) crates[c]=options[random(count)];
  }
}
void chooseJob(uint32_t now) {
  activeDigit=-1;
  for(int i=0;i<4;i++) if(shown[i]!=target[i]) { activeDigit=i; break; }
  respawnCrates();
  int options[20],count=0;
  // Occasionally explore a different passage before the next bombing run.
  bool patrol=activeDigit<0 && random(4)==0;
  for(int n=0;n<20;n++) {
    if(occupied(n) || n==heroNode) continue;
    int path[20],steps;
    if(!findRoute(heroNode,n,path,steps)) continue;
    int lengths[4],digits[4],boxes[4]; traceFlames(n,lengths,digits,boxes);
    bool useful=patrol;
    for(int d=0;d<4;d++) if(activeDigit>=0?digits[d]==activeDigit:boxes[d]>=0) useful=true;
    if(useful && escapeNode(n,lengths)>=0) options[count++]=n;
  }
  if(!count) { phase=PATROL; routeCount=routeIndex=0; phaseStart=now; return; }
  bombNode=options[random(count)]; goTo(bombNode);
  phase=patrol?PATROL:APPROACH; phaseStart=now;
}
void plantBomb(uint32_t now) {
  traceFlames(bombNode,flameLength,flameDigit,flameCrate);
  int safe=escapeNode(bombNode,flameLength);
  if(safe<0 || !goTo(safe)) { chooseJob(now); return; }
  int distance=0,prev=heroNode;
  for(int j=0;j<routeCount;j++) { distance+=abs(nx(route[j])-nx(prev))+abs(ny(route[j])-ny(prev)); prev=route[j]; }
  fuseDuration=1400;
  uint32_t escapeTime=(uint32_t)(distance*1000/48+350);
  if(escapeTime>fuseDuration) fuseDuration=escapeTime;
  phase=FUSE; phaseStart=now;
}

void brick(int x,int y,uint16_t color) {
  display.fillRect(x,y,3,3,color); display.drawPixel(x+2,y+2,0x4208);
}
void drawHero(uint32_t now) {
  int x=(int)heroX,y=(int)heroY;
  bool moving=routeIndex<routeCount;
  int step=moving?(now/100)%2:0;
  // 5x8 sprite, including helmet bobble. Side and rear views follow the route.
  display.drawPixel(x,y-4,pink);
  display.fillRect(x-2,y-3,5,3,0xFFFF);
  if(facing!=3) {
    display.fillRect(x-1,y-2,3,2,0xFE75);
    if(facing==0) display.drawPixel(x+1,y-1,0);
    else if(facing==2) display.drawPixel(x-1,y-1,0);
    else { display.drawPixel(x-1,y-1,0); display.drawPixel(x+1,y-1,0); }
  }
  display.fillRect(x-1,y,3,2,blue);
  display.drawPixel(x-2,y+step,pink); display.drawPixel(x+2,y+1-step,pink);
  display.fillRect(x-1,y+2,1,1+step,pink);
  display.fillRect(x+1,y+2,1,2-step,pink);
}
}

void resetBombermanAnimation() { initialized=false; }

void displayClockWithBomberman() {
  struct tm t;
  if(!getTimeWithTimeout(&t)) {
    display.setTextSize(1); display.setTextColor(0xFFFF);
    display.setCursor(20,28); display.print("Syncing time..."); return;
  }
  int hour,minute; bool pm;
  formatTimeForDisplay(t.tm_hour,t.tm_min,hour,minute,pm);
  target[0]=hour/10; target[1]=hour%10; target[2]=minute/10; target[3]=minute%10;
  uint32_t now=millis();
  if(!initialized) {
    for(int i=0;i<4;i++) shown[i]=target[i];
    heroNode=15; heroX=nx(heroNode); heroY=ny(heroNode); facing=0;
    crates[0]=6; crates[1]=13; bonusNode=-1;
    initialized=true; lastFrame=now; chooseJob(now);
  }
  float dt=fminf((now-lastFrame)/1000.0f,0.05f); lastFrame=now;
  uint32_t age=now-phaseStart;
  if(phase==PATROL || phase==APPROACH) {
    if(moveHero(dt,30)) {
      bool pending=false;
      for(int i=0;i<4;i++) if(shown[i]!=target[i]) pending=true;
      if(phase==PATROL || (activeDigit<0 && pending)) chooseJob(now);
      else plantBomb(now);
    }
  } else if(phase==FUSE) {
    bool safe=moveHero(dt,48);
    if(age>=fuseDuration && safe) {
      phase=BLAST; phaseStart=now;
      for(int d=0;d<4;d++) if(flameCrate[d]>=0) {
        int c=flameCrate[d];
        if(crates[c]>=0) { bonusNode=crates[c]; crates[c]=-1; }
      }
    }
  } else if(phase==BLAST && age>=650) {
    if(activeDigit>=0) { phase=BUILD; phaseStart=now; }
    else if(bonusNode>=0 && goTo(bonusNode)) { phase=COLLECT; phaseStart=now; }
    else chooseJob(now);
  } else if(phase==BUILD && age>=850) {
    shown[activeDigit]=target[activeDigit]; chooseJob(now);
  } else if(phase==COLLECT && moveHero(dt,34)) {
    bonusNode=-1; chooseJob(now);
  }
  age=now-phaseStart;

  display.drawFastHLine(0,1,128,0x2204);
  display.drawFastHLine(0,61,128,0x2204);
  // Faint paving gives the movement a top-down arcade setting.
  for(int x=6;x<128;x+=8) {
    display.drawPixel(x,8,0x1082); display.drawPixel(x,52,0x1082);
  }
  for(int i=0;i<5;i++) for(int y=12;y<50;y+=6) display.drawPixel(laneX[i],y,0x1082);
  for(int i=0;i<4;i++) {
    bool changing=i==activeDigit;
    if(changing && phase==BLAST) {
      float s=age/650.0f;
      for(int r=0;r<7;r++) for(int c=0;c<5;c++) if(glyph[shown[i]][r]&(16>>c)) {
        int x=digitX[i]+c*4+(int)((c-2)*s*13);
        int y=digitY+r*4+(int)(-18*s+42*s*s+(r-3)*s*5);
        if(age<480) display.fillRect(x,y,2,2,digitColor());
      }
      continue;
    }
    int value=changing && phase==BUILD?target[i]:shown[i];
    for(int r=0;r<7;r++) for(int c=0;c<5;c++) {
      if(!(glyph[value][r]&(16>>c))) continue;
      if(changing && phase==BUILD && age<(uint32_t)((6-r)*95+c*18)) continue;
      brick(digitX[i]+c*4,digitY+r*4,digitColor());
    }
  }
  // The colon is a pair of floor lights in the central passage.
  if(shouldShowColon()) {
    display.fillRect(63,25,2,2,digitColor()); display.fillRect(63,37,2,2,digitColor());
  }
  for(int c=0;c<2;c++) if(crates[c]>=0) {
    int x=nx(crates[c])-3,y=ny(crates[c])-3;
    display.fillRect(x,y,7,7,0xA285); display.drawRect(x,y,7,7,0xFCCC);
    display.drawLine(x+1,y+1,x+5,y+5,0xFCCC);
  }
  if(bonusNode>=0 && phase!=BLAST) {
    int x=nx(bonusNode),y=ny(bonusNode);
    display.drawRect(x-2,y-2,5,5,(now/180)%2?0xFFE0:0x07FF);
    display.drawPixel(x,y,0xFFFF);
  }
  if(phase==FUSE) {
    int x=nx(bombNode),y=ny(bombNode);
    display.fillCircle(x,y,2,0x528A); display.drawPixel(x-1,y-1,0xFFFF);
    display.drawPixel(x+1,y-3,(now/70)%2?0xFFE0:orange);
  }
  if(phase==BLAST) {
    int x=nx(bombNode),y=ny(bombNode);
    for(int d=0;d<4;d++) {
      int reach=flameLength[d];
      if(age<110) reach=reach*age/110;
      for(int s=0;s<=reach;s++) {
        int px=x+dx[d]*s,py=y+dy[d]*s;
        display.fillRect(px-1,py-1,3,3,(now/70)%2?orange:0xFBE0);
        display.drawPixel(px,py,0xFFE0);
      }
    }
  }
  drawHero(now);
  if(!settings.use24Hour) drawMeridiemIndicator(108,3,pm);
  if(!wifiConnected) drawNoWiFiIcon(0,3);
}
