# VulkanRenderer

This is my study project, starting from September 2023, for a whole semester. The goal is to utilize the Vulkan graphics API to create a renderer that creates realistic and stunning real-time renders of cars and other objects with clear-coat pbr materials, utilizing raytraced shadows and reflections, but rasterization will be used for the rest of the rendering.

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

### Energy conservation in indirect specular (works for clear-coat specular too)
https://github.com/Contingencyy/VulkanRenderer/assets/34250026/c601a629-0510-4cf0-836e-0bb6769c17cc

## Planned features
- Raytraced soft shadows
- Raytraced reflections
- Raytraced ambient occlusion

## Stretch features
- Global illumination
- (Convolution) Bloom
- Dithering
- Lens flare
- LODding
- Mesh shaders (and meshlet builder)
