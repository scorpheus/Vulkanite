# Vulkanite

Vulkan toy renderer using :
* Vulkan 1.3 (https://www.vulkan.org/)
* PBR, Spherical Harmonics
* Raytracing (https://www.khronos.org/blog/
ray-tracing-in-vulkan)
* Rasterization
* Nvidia DLSS 2 (https://github.com/NVIDIA/DLSS)

NEXT: 
* add shadow from raytracing to rasterization
* GI

Requirements:
* Cmake
* Conan (if you have python just do "`pip install conan==1.59.0`")
* if after cmake, the code ask you some depencies, just run cmake configure/generate another time.
  
Screenshots:  
Full Raytracing  
![alt text](screenshots/screenshot1.jpg "Raytracing")
![alt text](screenshots/screenshot2.jpg "Raytracing")  
Full Rasterization  
![alt text](screenshots/screenshot3.jpg "Rasterization")
![alt text](screenshots/screenshot4.jpg "Rasterization")

Build USD:

cd extern/USD
git clone -b v22.05a --depth 1 https://github.com/PixarAnimationStudios/USD.git
cd USD
git apply ../usd.patch
cd ..      
mkdir build
C:\anaconda3\ppython USD\build_scripts\build_usd.py --build-variant release --no-python --no-tools --no-tutorials --no-docs --no-tests --no-examples --no-usdview --imaging --materialx --ptex --build-monolithic USD_build_Release
C:\anaconda3\python USD\build_scripts\build_usd.py --build-variant debug --no-python --no-tools --no-tutorials --no-docs --no-tests --no-examples --no-usdview --imaging --opencolorio --materialx --ptex --build-monolithic USD_build_Debug
