from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected exactly one match in {path}: found {count}\n--- old ---\n{old}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def patch_source_first_builder() -> None:
    path = ROOT / "Tools" / "Blender" / "build_antey_source_first.py"
    replace_once(
        path,
        "PRODUCTION_BEAM = 18.2\nLONG_SCALE = PRODUCTION_LENGTH / SOURCE_LENGTH\n",
        "PRODUCTION_BEAM = 18.2\n"
        "# Production material values are scene-linear. The upper hull is deliberately technical black:\n"
        "# shape readability comes from dielectric reflections and underwater lighting, not grey albedo.\n"
        "ANTEY_TECHNICAL_BLACK_LINEAR = (0.001517635, 0.001517635, 0.001517635, 1.0)  # sRGB #050505\n"
        "ANTEY_ANTIFOULING_RED_LINEAR = (0.068478170, 0.004776953, 0.003676507, 1.0)  # sRGB #4A0F0C\n"
        "ANTEY_LOWER_HULL_SPLIT_Z_M = -0.65\n"
        "LONG_SCALE = PRODUCTION_LENGTH / SOURCE_LENGTH\n",
    )
    replace_once(
        path,
        "def material(name: str, colour: tuple[float, float, float, float]) -> bpy.types.Material:\n"
        "    result = bpy.data.materials.get(name) or bpy.data.materials.new(name)\n"
        "    result.diffuse_color = colour\n"
        "    result.use_nodes = True\n"
        "    principled = result.node_tree.nodes.get(\"Principled BSDF\")\n"
        "    if principled:\n"
        "        principled.inputs[\"Base Color\"].default_value = colour\n"
        "        principled.inputs[\"Roughness\"].default_value = 0.72\n"
        "        principled.inputs[\"Metallic\"].default_value = 0.18\n"
        "    return result\n",
        "def material(\n"
        "    name: str, colour: tuple[float, float, float, float], roughness: float = 0.72, metallic: float = 0.18\n"
        ") -> bpy.types.Material:\n"
        "    result = bpy.data.materials.get(name) or bpy.data.materials.new(name)\n"
        "    result.diffuse_color = colour\n"
        "    result.use_nodes = True\n"
        "    principled = result.node_tree.nodes.get(\"Principled BSDF\")\n"
        "    if principled:\n"
        "        principled.inputs[\"Base Color\"].default_value = colour\n"
        "        principled.inputs[\"Roughness\"].default_value = roughness\n"
        "        principled.inputs[\"Metallic\"].default_value = metallic\n"
        "    return result\n",
    )
    replace_once(
        path,
        "    mesh.from_pydata(vertices, [], polygons)\n"
        "    mesh.update(calc_edges=True)\n"
        "    obj = bpy.data.objects.new(name, mesh)\n"
        "    bpy.context.scene.collection.objects.link(obj)\n"
        "    obj.data.materials.append(prop_mat if role.startswith(\"PROPELLER\") else hull_mat)\n",
        "    mesh.from_pydata(vertices, [], polygons)\n"
        "    mesh.update(calc_edges=True)\n"
        "    # Source-first partitioning recreated polygon objects but previously dropped the source smooth-shading\n"
        "    # state. That made curved hull stations render as visibly faceted flat polygons in the runtime GLB.\n"
        "    for polygon in mesh.polygons:\n"
        "        polygon.use_smooth = True\n"
        "    obj = bpy.data.objects.new(name, mesh)\n"
        "    bpy.context.scene.collection.objects.link(obj)\n"
        "    if role.startswith(\"PROPELLER\"):\n"
        "        obj.data.materials.append(prop_mat)\n"
        "    else:\n"
        "        obj.data.materials.append(hull_mat)\n"
        "        lower_hull_mat = bpy.data.materials.get(\"MAT_Antey_LowerHull\")\n"
        "        if lower_hull_mat is not None:\n"
        "            obj.data.materials.append(lower_hull_mat)\n"
        "            for polygon in mesh.polygons:\n"
        "                polygon.material_index = 1 if polygon.center.z <= ANTEY_LOWER_HULL_SPLIT_Z_M else 0\n"
        "    mesh.update()\n",
    )
    replace_once(
        path,
        "    hull_mat = material(\"MAT_Antey_Hull\", (0.23, 0.28, 0.31, 1.0))\n"
        "    prop_mat = material(\"MAT_Antey_Propellers\", (0.36, 0.25, 0.10, 1.0))\n",
        "    hull_mat = material(\"MAT_Antey_Hull\", ANTEY_TECHNICAL_BLACK_LINEAR, roughness=0.86, metallic=0.0)\n"
        "    material(\"MAT_Antey_LowerHull\", ANTEY_ANTIFOULING_RED_LINEAR, roughness=0.82, metallic=0.0)\n"
        "    prop_mat = material(\"MAT_Antey_Propellers\", (0.36, 0.25, 0.10, 1.0), roughness=0.72, metallic=0.18)\n",
    )


def patch_model_shader() -> None:
    path = ROOT / "Shaders" / "Model.hlsl"
    replace_once(
        path,
        "    const float3 viewDirection = float3(0.0F, 0.0F, 1.0F);\n"
        "    const float3 halfVector = normalize(toLight + viewDirection);\n"
        "    const float specularPower = lerp(64.0F, 4.0F, saturate(Roughness));\n"
        "    const float specularAmount = pow(saturate(dot(normal, halfVector)), specularPower);\n"
        "    const float3 specularColor = lerp(0.04F.xxx, BaseColor.rgb, saturate(Metallic));\n",
        "    // Orthographic production view direction is frame-owned. The old hard-coded +Z vector made the\n"
        "    // highlight move incorrectly as the combat camera changed orientation and exaggerated hull facets.\n"
        "    const float3 viewDirection = normalize(-CameraViewDirection);\n"
        "    const float3 halfVector = normalize(toLight + viewDirection);\n"
        "    const float specularPower = lerp(64.0F, 4.0F, saturate(Roughness));\n"
        "    const float specularAmount = pow(saturate(dot(normal, halfVector)), specularPower);\n"
        "    const float3 specularF0 = lerp(0.04F.xxx, BaseColor.rgb, saturate(Metallic));\n"
        "    const float oneMinusNdotV = 1.0F - saturate(dot(normal, viewDirection));\n"
        "    const float fresnelWeight = oneMinusNdotV * oneMinusNdotV * oneMinusNdotV * oneMinusNdotV * oneMinusNdotV;\n"
        "    const float3 specularColor = specularF0 + (1.0F.xxx - specularF0) * fresnelWeight;\n",
    )


def patch_tonemap_shader() -> None:
    path = ROOT / "Shaders" / "PostProcess" / "ToneMap.hlsl"
    replace_once(
        path,
        "float4 PSMain(FullscreenPixelInput input) : SV_TARGET\n"
        "{\n"
        "    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);\n"
        "    return float4(LinearToSrgb(ToneMapSceneLinear(scene.rgb)), scene.a);\n"
        "}\n\n"
        "float4 PSHdrScRgb(FullscreenPixelInput input) : SV_TARGET\n"
        "{\n"
        "    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);\n"
        "    return float4(MapSceneLinearToHdrScRgb(scene.rgb), scene.a);\n"
        "}\n",
        "float InterleavedGradientNoise(const float2 pixelPosition)\n"
        "{\n"
        "    // Deterministic screen-space dither. SceneColorHDR remains FP16; noise is introduced only at the\n"
        "    // final display conversion so smooth dark-water gradients do not quantize into visible bands.\n"
        "    return frac(52.9829189F * frac(dot(pixelPosition, float2(0.06711056F, 0.00583715F))));\n"
        "}\n\n"
        "float3 DitherForQuantization(const float3 value, const float quantizationSteps, const float2 pixelPosition)\n"
        "{\n"
        "    const float centeredNoise = InterleavedGradientNoise(pixelPosition) - 0.5F;\n"
        "    return value + centeredNoise / quantizationSteps;\n"
        "}\n\n"
        "float4 PSMain(FullscreenPixelInput input) : SV_TARGET\n"
        "{\n"
        "    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);\n"
        "    const float3 encoded = LinearToSrgb(ToneMapSceneLinear(scene.rgb));\n"
        "    return float4(saturate(DitherForQuantization(encoded, 255.0F, input.position.xy)), scene.a);\n"
        "}\n\n"
        "float4 PSHdrScRgb(FullscreenPixelInput input) : SV_TARGET\n"
        "{\n"
        "    const float4 scene = SceneColorHDR.Sample(SceneColorSampler, input.uv);\n"
        "    const float3 scRgb = MapSceneLinearToHdrScRgb(scene.rgb);\n"
        "    // The swap chain is FP16 scRGB, but common HDR scan-out is 10-bit. Half an LSB of static dither\n"
        "    // prevents the compositor/display quantization from exposing dark-ocean contour bands.\n"
        "    return float4(max(DitherForQuantization(scRgb, 1023.0F, input.position.xy), 0.0F.xxx), scene.a);\n"
        "}\n",
    )


def patch_ocean_presentation() -> None:
    path = ROOT / "Game" / "PhysicalPlayground.cpp"
    replace_once(
        path,
        "constexpr std::array<float, 3> M3DepthAttenuationPerMeterRgb{0.012F, 0.006F, 0.003F};\n"
        "constexpr std::array<float, 3> M3DeepAmbientRgb{0.02F, 0.075F, 0.12F};\n",
        "// Effective open-ocean coefficients chosen to preserve the real spectral ordering while keeping\n"
        "// Deep Run readable: red disappears quickly, green follows, and blue survives deepest. NOAA's\n"
        "// qualitative photic/twilight boundary remains the reference, not a claim of one universal IOP set.\n"
        "constexpr std::array<float, 3> M3DepthAttenuationPerMeterRgb{0.045F, 0.020F, 0.010F};\n"
        "constexpr std::array<float, 3> M3DeepAmbientRgb{0.006F, 0.024F, 0.050F};\n",
    )
    replace_once(
        path,
        "constexpr float M3FogExtinctionPerMeter = 0.004F;\n"
        "constexpr std::array<float, 3> M3FogColorRgb{\n"
        "    M2UnderwaterBackgroundColor.r,\n"
        "    M2UnderwaterBackgroundColor.g,\n"
        "    M2UnderwaterBackgroundColor.b};\n",
        "constexpr float M3FogExtinctionPerMeter = 0.0035F;\n"
        "constexpr std::array<float, 3> M3FogColorRgb{\n"
        "    M2UnderwaterBackgroundColor.r,\n"
        "    M2UnderwaterBackgroundColor.g,\n"
        "    M2UnderwaterBackgroundColor.b};\n"
        "constexpr std::array<float, 3> M3DeepFogColorRgb{0.0008F, 0.0060F, 0.0180F};\n"
        "constexpr float M3DeepFogReferenceDepthMeters = 700.0F;\n",
    )
    replace_once(
        path,
        "    const Render::ScenePresentationParameters scenePresentation{\n"
        "        .depthLighting = {\n"
        "            .surfaceLevelYMeters = water_->Config().surfaceLevelY,\n"
        "            .attenuationPerMeterRgb = M3DepthAttenuationPerMeterRgb,\n"
        "            .deepAmbientRgb = M3DeepAmbientRgb},\n"
        "        .cameraPlaneCenterWorldPosition = {camera->position.x, camera->position.y, camera->position.z},\n"
        "        .cameraViewDirection = {camera->viewDirection.x, camera->viewDirection.y, camera->viewDirection.z},\n"
        "        .fogExtinctionPerMeter = M3FogExtinctionPerMeter,\n"
        "        .fogColorRgb = M3FogColorRgb};\n",
        "    const float cameraDepthMeters = std::max(water_->Config().surfaceLevelY - camera->position.y, 0.0F);\n"
        "    const float deepFogBlend = std::clamp(cameraDepthMeters / M3DeepFogReferenceDepthMeters, 0.0F, 1.0F);\n"
        "    const auto blendChannel = [deepFogBlend](const float shallow, const float deep) noexcept\n"
        "    { return shallow + (deep - shallow) * deepFogBlend; };\n"
        "    const std::array<float, 3> depthAwareFogColorRgb{\n"
        "        blendChannel(M3FogColorRgb[0], M3DeepFogColorRgb[0]),\n"
        "        blendChannel(M3FogColorRgb[1], M3DeepFogColorRgb[1]),\n"
        "        blendChannel(M3FogColorRgb[2], M3DeepFogColorRgb[2])};\n"
        "    const Render::ScenePresentationParameters scenePresentation{\n"
        "        .depthLighting = {\n"
        "            .surfaceLevelYMeters = water_->Config().surfaceLevelY,\n"
        "            .attenuationPerMeterRgb = M3DepthAttenuationPerMeterRgb,\n"
        "            .deepAmbientRgb = M3DeepAmbientRgb},\n"
        "        .cameraPlaneCenterWorldPosition = {camera->position.x, camera->position.y, camera->position.z},\n"
        "        .cameraViewDirection = {camera->viewDirection.x, camera->viewDirection.y, camera->viewDirection.z},\n"
        "        .fogExtinctionPerMeter = M3FogExtinctionPerMeter,\n"
        "        .fogColorRgb = depthAwareFogColorRgb};\n",
    )

    water_path = ROOT / "Game" / "WaterPresentation.h"
    replace_once(
        water_path,
        "constexpr Render::RgbaColor M2UnderwaterBackgroundColor{0.00309598F, 0.03954624F, 0.11953843F, 1.0F};\n",
        "// Open-Pacific presentation base (approximately sRGB #062B46). Depth lighting and fog push this\n"
        "// progressively toward the deep-ocean floor instead of keeping one bright blue clear colour.\n"
        "constexpr Render::RgbaColor M2UnderwaterBackgroundColor{0.00182116F, 0.02415763F, 0.06124605F, 1.0F};\n",
    )


def main() -> None:
    patch_source_first_builder()
    patch_model_shader()
    patch_tonemap_shader()
    patch_ocean_presentation()
    print("Antey Pacific production visual source patch: PASS")


if __name__ == "__main__":
    main()
