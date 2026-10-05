#pragma once

// Improved Camera SE shows the 3rd person body in 1st person. While we show the 1st person right arm again for the pose
// (ArmPose::ShownAgain), the body's own right arm is hidden, so there is only one right arm in view.
namespace BodyArm
{
	void Hide(RE::NiAVObject* a_thirdPersonRoot);  // every frame while our arm is shown
	void Restore();                                // gives the body its arm back
	void Forget();                                 // game load: drop the node without touching it
}
