# Hime

Implementation of ray tracing techniques.

## Implementations
 - [Realtime Stochastic Lightcuts](RealtimeStochasticLightcuts/) [I3D 2020, [Paper Page](https://dqlin.xyz/pubs/2020-i3d-SLC/)]
 - [A-Trous Filtering](ATrousWaveletFilter/) [HPG 2010, [Paper Page](https://jo.dreggn.org/home/2010_atrous.pdf)]

## Usage

### Build
1. Clone [Falcor fork for Hime](https://github.com/VicentChen/Falcor): `git clone --recursive git@github.com:VicentChen/Falcor.git`.
2. Build. (Please follow official Falcor readme)
3. Download extra model files from: https://developer.nvidia.com/orca
4. Build entire solution.

### Run
1. Set `Mogwai` as start up project.
2. Load render graph file(`xxx.py`) in implementation folders.
3. Load scene.

### Notes
- For some scenes, z-fighting issues may occur. You may need to modify camera near plan(camera depth) to 0.01.
