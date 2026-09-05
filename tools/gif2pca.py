#!/usr/bin/env python3
"""Command-line entry point for the shared PixelClock GIF converter."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'PC-Companion-App-v4' / 'companion-common'))
from gif_converter import main
if __name__ == '__main__':
    main()
