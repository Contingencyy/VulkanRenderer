# VulkanRenderer

## Installation

To run this project yourself, simply download the source code from GitHub, as well as the latest Vulkan SDK here: https://vulkan.lunarg.com/sdk/home.
The solution will automatically try to find the Vulkan SDK installation by its environment variable VULKAN_SDK. Last tested with Vulkan SDK version 1.3.275.0.

## Project

This is my study project, starting from September 2023, for a whole semester. The goal is to utilize the Vulkan graphics API to create a renderer that creates realistic and stunning real-time renders of cars and other objects with clear-coat pbr materials, utilizing raytraced shadows and reflections, rasterization will be used for everything else.

## Current features
- Physically-based rendering
  - Lambertian/Burley/Oren-Nayar Diffuse BRDF
  - Clear-coat materials
  - Normal mapping
- Image-based lighting
  - Generated BRDF LUT
  - Generates from equirectangular to cubemaps
  - Energy conserving indirect specular (works for clear-coat specular too)
- Basic post-processing (Tonemap, gamma correction, exposure)

### PBR Chess scene, with image-based lighting

![PBR_Chess_IBL](https://github.com/Contingencyy/VulkanRenderer/assets/34250026/e74f3845-098a-4d7c-9d36-b01cc0242c84)

### Toy car with clear-coat enabled

![Toy_Car_Clear_Coat](https://github.com/Contingencyy/VulkanRenderer/assets/34250026/6960c37d-0111-487a-ac05-f4bde05b1733)

### Toy car with clear-coat disabled

![Toy_Car_No_Clear_Coat](https://github.com/Contingencyy/VulkanRenderer/assets/34250026/2673da23-093d-4262-a59a-ca93fc59d18e)

### Energy conservation in indirect specular (works for clear-coat specular too) - White Furnace Test
https://github.com/Contingencyy/VulkanRenderer/assets/34250026/6fc2cafc-1dc9-4111-a021-353d2a8c9426

## Planned features
- Raytraced soft shadows
- Raytraced reflections
- Raytraced ambient occlusion

## Stretch features
- (Convolution) Bloom
- Dithering
- LODding
- Mesh shaders (and meshlet builder)
