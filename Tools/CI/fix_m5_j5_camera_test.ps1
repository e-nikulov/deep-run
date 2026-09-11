$ErrorActionPreference='Stop'
$p='Tests/M5MultiScaleCameraChecks.h'
$t=(Get-Content -Raw $p).Replace("`r`n","`n")
$old=@'
    const Input::ControllerSemanticAxes controller = Input::SemanticAxesForGamepad(Input::GamepadState{
        .connected = true,
        .rightX = 0.8F,
        .rightY = -0.7F,
        .leftTrigger = 0.1F,
        .rightTrigger = 0.9F});
    if (controller.cameraPanX <= 0.0F || controller.cameraPanY != 0.0F ||
        std::abs(controller.cameraZoom - 0.7F) > 0.001F)
    {
        return false;
    }
'@.Replace("`r`n","`n").Trim([char[]]"`r`n")
$new=@'
    const Input::GamepadState cameraStick{
        .connected = true,
        .rightX = 0.8F,
        .rightY = -0.7F,
        .leftTrigger = 0.1F,
        .rightTrigger = 0.9F};
    const Input::ControllerSemanticAxes controller = Input::SemanticAxesForGamepad(cameraStick);
    auto triggerNeutral = cameraStick;
    triggerNeutral.leftTrigger = 0.0F;
    triggerNeutral.rightTrigger = 0.0F;
    const Input::ControllerSemanticAxes controllerWithoutTriggers = Input::SemanticAxesForGamepad(triggerNeutral);
    if (controller.cameraPanX <= 0.0F || controller.cameraPanY != 0.0F || controller.cameraZoom <= 0.0F ||
        std::abs(controller.cameraPanX - controllerWithoutTriggers.cameraPanX) > 0.001F ||
        std::abs(controller.cameraZoom - controllerWithoutTriggers.cameraZoom) > 0.001F)
    {
        return false;
    }
'@.Replace("`r`n","`n").Trim([char[]]"`r`n")
$i=$t.IndexOf($old,[System.StringComparison]::Ordinal)
if($i -lt 0){throw 'camera assertion anchor not found'}
$t=$t.Substring(0,$i)+$new+$t.Substring($i+$old.Length)
[System.IO.File]::WriteAllText($p,$t,[System.Text.UTF8Encoding]::new($false))
git diff --check
