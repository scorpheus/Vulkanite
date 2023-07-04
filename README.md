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

in x64 native tools command prompt for vs 2022
cd extern/USD
git clone https://github.com/PixarAnimationStudios/OpenUSD.git
C:\anaconda3\python USD\build_scripts\build_usd.py --build-variant release --generator "Visual Studio 17 2022" --toolset v143 --no-python --no-tools --no-tutorials --no-docs --no-tests --no-examples --no-usdview --imaging --materialx --ptex --build-monolithic USD_build_Release
C:\anaconda3\python USD\build_scripts\build_usd.py --build-variant debug --generator "Visual Studio 17 2022" --toolset v143 --no-python --no-tools --no-tutorials --no-docs --no-tests --no-examples --no-usdview --imaging --materialx --ptex --build-monolithic USD_build_Debug
 