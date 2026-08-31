SOURCE AUDIT

Original: supplied immutable source (historical filename intentionally omitted from generated documentation)
File size: 9671804 bytes
Blender version: 5.2.1 LTS
Scenes: 1 (Scene)
Collections: 1

Objects: 6
Mesh objects: 2
Vertices: 55297
Triangles: 109080
Materials: 11
Textures: 7
Modifiers: 1
Armatures: 0
Empties: 0

Dimensions: (1.7063244581222534, 9.775832176208496, 1.782830387353897)
Length: 1.706 m (authored world X)
Width: 9.776 m (authored world Y)
Height: 1.783 m (authored world Z)

Suspected print-only/internal geometry:
- Bridge
- Hull

Reusable exterior geometry:
- none

Problematic geometry:
- Bridge: {'vertices': 49550, 'edges': 97259, 'polygons': 47703, 'triangles': 97954, 'components': 33, 'non_manifold_edges': 1363, 'degenerate_faces': 178, 'has_uv': True, 'has_material': True, 'identity_rotation': True, 'identity_scale': False}
- Hull: {'vertices': 5747, 'edges': 14084, 'polygons': 8353, 'triangles': 11126, 'components': 9, 'non_manifold_edges': 464, 'degenerate_faces': 37, 'has_uv': True, 'has_material': True, 'identity_rotation': False, 'identity_scale': False}

Per-object audit:
- `Bridge`: {'vertices': 49550, 'edges': 97259, 'polygons': 47703, 'triangles': 97954, 'components': 33, 'non_manifold_edges': 1363, 'degenerate_faces': 178, 'has_uv': True, 'has_material': True, 'identity_rotation': True, 'identity_scale': False}
- `Hull`: {'vertices': 5747, 'edges': 14084, 'polygons': 8353, 'triangles': 11126, 'components': 9, 'non_manifold_edges': 464, 'degenerate_faces': 37, 'has_uv': True, 'has_material': True, 'identity_rotation': False, 'identity_scale': False}

Disconnected component bounds (world space):
- `Bridge` component 1: vertices=36131, edges=71150, min=(-0.865, -4.8754, -0.2051), max=(0.8413, 4.9005, 0.5868)
- `Bridge` component 2: vertices=3336, edges=6476, min=(-0.205, 1.0093, 0.445), max=(0.1789, 2.8168, 0.7798)
- `Bridge` component 3: vertices=1107, edges=2190, min=(-0.2209, -0.3014, 0.4363), max=(0.1993, 1.2266, 0.5286)
- `Bridge` component 4: vertices=1088, edges=2112, min=(-0.0606, -3.136, 0.289), max=(0.0389, -3.0376, 0.3135)
- `Bridge` component 5: vertices=1088, edges=2112, min=(-0.0626, -3.136, 0.289), max=(0.037, -3.0376, 0.3135)
- `Bridge` component 6: vertices=960, edges=1904, min=(-0.0354, 1.6108, 0.7675), max=(0.0356, 1.6816, 1.1857)
- `Bridge` component 7: vertices=845, edges=1713, min=(-0.0931, 1.9565, 0.6497), max=(0.0841, 2.3517, 0.8204)
- `Bridge` component 8: vertices=784, edges=1552, min=(-0.041, 1.4813, 0.7639), max=(0.0361, 1.5579, 1.0776)
- `Bridge` component 9: vertices=576, edges=1136, min=(-0.0276, 1.7445, 0.7653), max=(-0.0095, 1.7626, 1.2981)
- `Bridge` component 10: vertices=560, edges=1104, min=(-0.1098, 1.8228, 0.73), max=(0.0063, 1.9385, 1.1349)
- `Bridge` component 11: vertices=549, edges=1081, min=(-0.0199, 2.5677, 0.683), max=(0.0016, 2.5893, 1.0812)
- `Bridge` component 12: vertices=481, edges=945, min=(-0.0226, 2.3838, 0.7628), max=(0.0072, 2.412, 1.0837)
- `Bridge` component 13: vertices=340, edges=642, min=(0.3772, -4.2501, -0.0658), max=(0.6858, -4.0008, -0.0156)
- `Bridge` component 14: vertices=340, edges=642, min=(-0.7095, -4.2501, -0.0658), max=(-0.4009, -4.0008, -0.0156)
- `Bridge` component 15: vertices=338, edges=655, min=(-0.3078, 1.8018, 0.7484), max=(-0.2068, 1.9475, 0.9868)
- `Bridge` component 16: vertices=288, edges=560, min=(-0.0243, 1.3809, 0.7653), max=(0.021, 1.426, 1.1417)
- `Bridge` component 17: vertices=222, edges=430, min=(-0.0149, 2.796, 0.7651), max=(-0.0112, 2.798, 0.7688)
- `Bridge` component 18: vertices=112, edges=208, min=(0.0897, 1.6867, 0.7748), max=(0.1018, 1.6987, 1.2254)
- `Bridge` component 19: vertices=64, edges=112, min=(-0.2212, 1.8012, 0.7504), max=(-0.2128, 1.9457, 0.7588)
- `Bridge` component 20: vertices=59, edges=86, min=(-0.074, -3.6676, -0.4324), max=(0.0504, -2.4436, -0.2338)
- `Bridge` component 21: vertices=55, edges=96, min=(0.1768, 2.0834, 0.5015), max=(0.1775, 2.241, 0.6854)
- `Bridge` component 22: vertices=48, edges=73, min=(-0.0384, -4.0836, -0.3824), max=(0.0148, -3.914, -0.2029)
- `Bridge` component 23: vertices=25, edges=40, min=(-0.1304, 0.4854, -0.4811), max=(0.1067, 0.5005, -0.4804)
- `Bridge` component 24: vertices=25, edges=40, min=(-0.2054, 2.0834, 0.5015), max=(-0.2047, 2.241, 0.6854)
- `Bridge` component 25: vertices=18, edges=27, min=(0.0145, -3.9137, -0.3816), max=(0.0146, -3.9079, -0.1966)
- `Bridge` component 26: vertices=18, edges=27, min=(-0.0383, -3.9137, -0.3816), max=(-0.0382, -3.9079, -0.1966)
- `Bridge` component 27: vertices=18, edges=25, min=(-0.0865, 2.7907, 0.5294), max=(0.0604, 2.8162, 0.6862)
- `Bridge` component 28: vertices=15, edges=22, min=(-0.1297, 0.6024, -0.4817), max=(0.1061, 0.604, -0.4811)
- `Bridge` component 29: vertices=12, edges=20, min=(0.1648, 2.5509, 0.6219), max=(0.1728, 2.5934, 0.6671)
- `Bridge` component 30: vertices=12, edges=20, min=(-0.1988, 2.5515, 0.6232), max=(-0.1912, 2.5927, 0.6662)
- `Bridge` component 31: vertices=12, edges=20, min=(-0.2016, 2.4916, 0.6318), max=(-0.2011, 2.5209, 0.6657)
- `Bridge` component 32: vertices=12, edges=20, min=(-0.2018, 2.442, 0.6457), max=(-0.2016, 2.4588, 0.6647)
- `Bridge` component 33: vertices=12, edges=19, min=(0.1749, 2.4909, 0.6316), max=(0.1754, 2.5219, 0.6662)
- `Hull` component 1: vertices=2478, edges=4723, min=(-0.0118, -4.8754, -0.4847), max=(0.724, 4.8994, -0.0141)
- `Hull` component 2: vertices=413, edges=793, min=(-0.0446, -2.1318, 0.4066), max=(0.0656, -2.0223, 0.4489)
- `Hull` component 3: vertices=408, edges=1224, min=(0.2656, -4.8313, -0.0771), max=(0.3653, -4.8021, -0.0414)
- `Hull` component 4: vertices=408, edges=1224, min=(0.2553, -4.8313, -0.027), max=(0.3335, -4.8021, 0.0488)
- `Hull` component 5: vertices=408, edges=1224, min=(0.2476, -4.8313, -0.1594), max=(0.3104, -4.8021, -0.0656)
- `Hull` component 6: vertices=408, edges=1224, min=(0.2135, -4.8313, -0.0117), max=(0.2455, -4.8021, 0.0912)
- `Hull` component 7: vertices=408, edges=1224, min=(0.1808, -4.8313, -0.1624), max=(0.2313, -4.8021, -0.0659)
- `Hull` component 8: vertices=408, edges=1224, min=(0.1299, -4.8313, -0.0275), max=(0.2174, -4.8021, 0.0437)
- `Hull` component 9: vertices=408, edges=1224, min=(0.1141, -4.8313, -0.0832), max=(0.2112, -4.8021, -0.0424)

Hidden objects: Camera
Source naming hits requiring neutralization in production: 7 historical identifiers detected; names intentionally omitted

Object transforms and local dimensions:
- `Bridge`: location=(-0.385114, 1.667202, 0.735174), rotation=(0.0, 0.0, 0.0), scale=(0.26895, 0.26895, 0.062824), dimensions=(1.706325, 9.775832, 1.779781)
- `Camera`: location=(-3.060745, 7.414078, 1.770437), rotation=(1.368281, -2e-06, 3.649956), scale=(1.0, 1.0, 1.0), dimensions=(0.0, 0.0, 0.0)
- `Hull`: location=(-0.011825, 0.032465, 0.0), rotation=(1.570796, -0.0, -0.0), scale=(0.624572, 0.624572, 0.624572), dimensions=(1.471622, 0.933658, 9.774734)
- `Sun.1`: location=(-5.998779, -4.840154, 7.07163), rotation=(0.72453, -1.24025, 0.0), scale=(1.0, 1.0, 1.0), dimensions=(0.0, 0.0, 0.0)
- `Sun.2`: location=(-5.998779, -21.183666, 7.07163), rotation=(0.72453, -1.24025, 0.0), scale=(1.0, 1.0, 1.0), dimensions=(0.0, 0.0, 0.0)
- `Sun.3`: location=(6.940448, 0.0, -5.185245), rotation=(3.99215, -1.117689, 0.0), scale=(1.0, 1.0, 1.0), dimensions=(0.0, 0.0, 0.0)

Image datablocks / texture paths:
- `Untitled.001`: `//..\..\..\Downloads` (0x0)
- historical source image identifier omitted (351x425)
- historical source image identifier omitted (340x336)
- historical source image identifier omitted (340x357)
- historical source image identifier omitted (1711x357)
- historical source image identifier omitted (1707x336)
- historical source image identifier omitted (264x326)

Notes:
- This report is read-only and does not modify the supplied source blend.
- Source dimensions are not calibrated to DeepRun metres; production normalization is a separate step.
- Static heuristics cannot prove artistic intent; print-only/internal findings require visual review.
- License/attribution status must be reviewed separately before shipping.
