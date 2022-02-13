![image-20220211211828164](readme.assets/image-20220211211828164.png)



一共编写了 6 种物体，分别是 圆锥、三棱锥、三棱台、四棱锥、三棱柱、圆环：

```C++
MeshData CreateCone(float bottomRadius, float height, uint32 sliceCount, uint32 stackCount);
MeshData CreatePyramid1(float bottomEdge, float height, uint32 numSubdivisions);
MeshData CreatePyramid2(float bottomEdge, float topEdge, float height, uint32 numSubdivisions);
MeshData CreateSquarePyramid(float bottomEdge, float height, uint32 numSubdivisions);
MeshData CreateTriangularPrism(float bottomEdge, float height, uint32 numSubdivisions);
MeshData CreateTorus(float torusRadius, float tubeRadius, uint32 sliceCount, uint32 stackCount);
```

更改文件： GeometryGenerator.h/cpp，CastleApp.cpp。
