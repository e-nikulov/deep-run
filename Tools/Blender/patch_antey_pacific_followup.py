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
        "ANTEY_TECHNICAL_BLACK_LINEAR = (0.001517635, 0.001517635, 0.001517635, 1.0)  # sRGB #050505\n",
        "ANTEY_TECHNICAL_BLACK_LINEAR = (0.0, 0.0, 0.0, 1.0)  # exact sRGB/linear #000000 albedo\n",
    )

    replace_once(
        path,
        "    if role.startswith(\"PROPELLER\"):\n"
        "        obj.data.materials.append(prop_mat)\n"
        "    else:\n"
        "        obj.data.materials.append(hull_mat)\n"
        "        lower_hull_mat = bpy.data.materials.get(\"MAT_Antey_LowerHull\")\n"
        "        if lower_hull_mat is not None:\n"
        "            obj.data.materials.append(lower_hull_mat)\n"
        "            for polygon in mesh.polygons:\n"
        "                polygon.material_index = 1 if polygon.center.z <= ANTEY_LOWER_HULL_SPLIT_Z_M else 0\n",
        "    if role.startswith(\"PROPELLER\"):\n"
        "        obj.data.materials.append(prop_mat)\n"
        "    elif role == \"MAIN_HULL_LOWER\":\n"
        "        lower_hull_mat = bpy.data.materials.get(\"MAT_Antey_LowerHull\")\n"
        "        if lower_hull_mat is None:\n"
        "            raise RuntimeError(\"MAT_Antey_LowerHull must exist before lower-hull component creation\")\n"
        "        obj.data.materials.append(lower_hull_mat)\n"
        "    else:\n"
        "        obj.data.materials.append(hull_mat)\n",
    )


def patch_black_preserving_dither() -> None:
    path = ROOT / "Shaders" / "PostProcess" / "ToneMap.hlsl"
    replace_once(
        path,
        "float3 DitherForQuantization(const float3 value, const float quantizationSteps, const float2 pixelPosition)\n"
        "{\n"
        "    const float centeredNoise = InterleavedGradientNoise(pixelPosition) - 0.5F;\n"
        "    return value + centeredNoise / quantizationSteps;\n"
        "}\n",
        "float3 DitherForQuantization(const float3 value, const float quantizationSteps, const float2 pixelPosition)\n"
        "{\n"
        "    const float centeredNoise = InterleavedGradientNoise(pixelPosition) - 0.5F;\n"
        "    // Fade the dither in across the first code value. Exact black therefore remains exact black,\n"
        "    // while dark ocean gradients still receive enough decorrelation before display quantization.\n"
        "    const float3 ditherWeight = saturate(max(value, 0.0F.xxx) * quantizationSteps);\n"
        "    return value + (centeredNoise / quantizationSteps) * ditherWeight;\n"
        "}\n",
    )


def patch_deep_water_gradient() -> None:
    presentation = ROOT / "Game" / "Environment" / "ScalableEnvironmentPresentation.h"
    replace_once(
        presentation,
        "    constexpr std::size_t BandCount = 32U;\n",
        "    // ClearRect is currently the renderer-neutral fallback for this gradient. 32 discrete bands were\n"
        "    // themselves visible as colour stairs even on a 10-bit display; 256 keeps each scene-linear step\n"
        "    // below the presentation quantization scale without changing simulation or bathymetry authority.\n"
        "    constexpr std::size_t BandCount = 256U;\n",
    )

    checks = ROOT / "Tests" / "M5ScalableEnvironmentPresentationChecks.h"
    replace_once(
        checks,
        "abyssBands->size() != 32U",
        "abyssBands->size() != 256U",
    )


def main() -> None:
    patch_source_first_builder()
    patch_black_preserving_dither()
    patch_deep_water_gradient()
    print("Antey Pacific visual follow-up patch: PASS")


if __name__ == "__main__":
    main()
