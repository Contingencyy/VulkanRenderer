# VulkanRenderer

This is my study project, starting from September 2023, for a whole semester. The goal is to utilize the Vulkan graphics API to create a renderer that creates realistic and stunning real-time renders of cars and other objects with clear-coat pbr materials, utilizing raytraced shadows and reflections, but rasterization will be used for the rest of the rendering.

## Current features
- Physically-based rendering
  - Clear-coat materials
  - Normal mapping
- Image-based lighting
  - Energy conserving indirect specular
- Basic post-processing (Tonemap, gamma correction, exposure)

![PBR_Chess_Normal_Mapping](https://github.com/Contingencyy/VulkanRenderer/assets/34250026/89f9537e-288e-4d17-8f7b-b4fd9d43663a)

## Planned features
- Raytraced soft shadows
- Raytraced reflections
- Raytraced ambient occlusion
- Physically-based rendering with clear-coat materials
- Mipmapping

## Stretch features
- Global illumination
- (Convolution) Bloom
- Dithering
- Lens flare
- Bigger scenes
- Data streaming
- LODding
