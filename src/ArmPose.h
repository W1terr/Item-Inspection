#pragma once

// Procedural pose of the player's right arm (1st person skeleton, or the 3rd person body): two-bone IK (upper arm,
// forearm) to a wrist target, the hand turned to a wanted finger / palm direction, the wrist twist spread over the
// forearm twist bones, fingers curled a little or closed around a weapon. Skin bones the skeleton lacks (null in the
// skin) are moved along with the posed forearm / hand. The result is blended over the animation's pose by a weight,
// so the arm moves smoothly in and out of it.
//
// Must run after the animation wrote the bones of this frame (1st person: the camera update; 3rd person: right after
// the player's skeletons were updated).
// Only rotations are changed. Bones the animation didn't rewrite since our last call keep their animated values from
// our own cache, so calling it several times per frame doesn't stack.
namespace ArmPose
{
	struct Goal
	{
		RE::NiPoint3 wrist;      // world position for the hand bone (wrist)
		RE::NiPoint3 pole;       // world direction the elbow bends towards
		RE::NiPoint3 fingers;    // world direction the fingers point to
		RE::NiPoint3 palm;       // world direction the palm faces
		std::optional<RE::NiMatrix3> hand;  // if set: the hand's world rotation (instead of fingers / palm)
		float        weight{ 0.0f };  // 0 = animation, 1 = this goal
		std::array<float, 5> curl{};  // extra bend per joint for each finger (thumb first), radians
		bool                 weaponGrip{ false };  // fingers in the game's own grip around a weapon handle (instead of curl)
		bool                 hinge{ false };       // the elbow only bends around its hinge, the wrist bends and turns within
		                                           // limits: for an arm posed far from its animation (3rd person, hanging at the side)
		float                open{ 0.0f };         // fingers straightened towards the hand mesh's open hand (its bind pose), 0..1
		float                wristBend{ 0.6f };    // hinged arm: radians the wrist may bend away from its animation
		float                elbowBend{ 0.0f };    // radians the elbow bends more than the wrist target needs (the hand
		                                           // comes in towards the shoulder along the same line)
	};

	struct Hand
	{
		RE::NiTransform world;        // hand bone after posing
		RE::NiPoint3    palmCenter;   // world
		RE::NiPoint3    palmNormal;   // world, out of the palm
	};

	// The hand bone's own axes (hand local space), from the finger bones
	struct HandFrame
	{
		RE::NiPoint3                   fingerAxis;  // towards the middle finger
		RE::NiPoint3                   palmAxis;    // out of the palm
		float                          palmLength{ 0.0f };
		std::optional<RE::NiTransform> weaponGrip;  // the WEAPON node (where held weapons sit) relative to the hand
	};
	std::optional<HandFrame> Frame(RE::NiAVObject* a_root);

	// a_root: the player's 1st person skeleton or 3rd person body. Returns the posed hand, nothing if the arm isn't found.
	std::optional<Hand> Apply(RE::NiAVObject* a_root, const Goal& a_goal);

	// Gives the bones back to the animation (writes the animated values back once) and forgets them
	void Release();

	// The skeleton was unloaded (game load): drop the cached bones without touching them
	void Forget();

	// True while another mod hides the 1st person arm by shrinking the upper arm (Improved Camera SE does that when it
	// shows the 3rd person body in 1st person) and we show it again for the pose
	bool ShownAgain();
}
