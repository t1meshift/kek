# KEK engine

## Overview
KEK is a 2D/3D software renderer/engine written in pure C99 with no dynamic allocations. Its goal is to be compatible with as many platforms as possible.
Also it aims at having almost non-existant CPU or memory requirements.

## Structure
Right now, it has 3 components:
- The engine itself
- "GAME" with game/business logic
- Platform implementation files (only Win32 for now.)
