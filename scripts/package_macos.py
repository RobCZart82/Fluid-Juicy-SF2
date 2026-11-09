#!/usr/bin/env python3
"""Compatibility entry point for the native macOS arm64 packaging workflow."""
import platform
from package_desktop import main

if __name__ == '__main__':
    if platform.system() != 'Darwin':
        raise RuntimeError('Use package_desktop.py for Windows packaging')
    main()
