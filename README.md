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
