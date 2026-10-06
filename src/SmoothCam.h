#pragma once

// SmoothCam moves the 3rd person camera itself. While the camera flies in over the shoulder we ask it for the camera
// (and its crosshair) through its modder API and give both back afterwards. Without SmoothCam all of this does nothing.
namespace SmoothCam
{
	void Listen();   // SKSE kPostLoad: wait for SmoothCam's interface
	void Request();  // SKSE kPostPostLoad: ask for it

	void TakeCamera();  // SmoothCam leaves the camera alone until GiveBack; asking again while we have it does nothing
	void GiveBack();
	bool HasCamera();   // we may move the camera: it's ours, or SmoothCam isn't there
}
