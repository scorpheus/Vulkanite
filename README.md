# Vulkanite

Vulkan toy renderer using :
* Vulkan 1.3 (https://www.vulkan.org/)
* PBR, Spherical Harmonics
* Raytracing (https://www.khronos.org/blog/ray-tracing-in-vulkan)
* Nvidia DLSS 2 (https://github.com/NVIDIA/DLSS)

NEXT: 
* add shadow from raytracing to rasterization
* GI
* Moving objects

Requirements:
* Cmake
* Conan (if you have python just do "`pip install conan`")
* if after cmake, the code ask you some depencies, just run cmake configur/generate another time.
  
Screenshots:
![alt text](screenshots/screenshot1.jpg "Raytracing")
![alt text](screenshots/screenshot2.jpg "Raytracing")
![alt text](screenshots/screenshot3.jpg "Rasterization")
![alt text](screenshots/screenshot4.jpg "Rasterization")