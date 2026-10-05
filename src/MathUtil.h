#pragma once

// Small vector / rotation helpers on top of NiPoint3 / NiMatrix3.
// Rotation matrices map local vectors to parent / world vectors: world = R * local (column vectors).
namespace MathUtil
{
	using RE::NiMatrix3;
	using RE::NiPoint3;
	using RE::NiTransform;

	inline float Clamp01(float a_value) { return std::clamp(a_value, 0.0f, 1.0f); }

	// 0..1 -> 0..1, eased at both ends
	inline float Smooth(float a_t)
	{
		const float t = Clamp01(a_t);
		return t * t * (3.0f - 2.0f * t);
	}

	inline NiPoint3 Lerp(const NiPoint3& a_from, const NiPoint3& a_to, float a_t) { return a_from + (a_to - a_from) * a_t; }

	inline NiPoint3 Normalized(const NiPoint3& a_vector, const NiPoint3& a_fallback = { 0.0f, 0.0f, 1.0f })
	{
		const float length = a_vector.Length();
		return length > 1e-5f ? a_vector / length : a_fallback;
	}

	// matrix with the given vectors as columns
	inline NiMatrix3 FromColumns(const NiPoint3& a_x, const NiPoint3& a_y, const NiPoint3& a_z)
	{
		NiMatrix3 m;
		for (int row = 0; row < 3; ++row) {
			m.entry[row][0] = a_x[row];
			m.entry[row][1] = a_y[row];
			m.entry[row][2] = a_z[row];
		}
		return m;
	}

	inline NiPoint3 Column(const NiMatrix3& a_matrix, int a_column)
	{
		return { a_matrix.entry[0][a_column], a_matrix.entry[1][a_column], a_matrix.entry[2][a_column] };
	}

	// rotation by a_angle radians around a unit axis (right hand rule)
	inline NiMatrix3 AxisAngle(const NiPoint3& a_axis, float a_angle)
	{
		const float c = std::cos(a_angle);
		const float s = std::sin(a_angle);
		const float t = 1.0f - c;
		const float x = a_axis.x, y = a_axis.y, z = a_axis.z;
		NiMatrix3   m;
		m.entry[0][0] = t * x * x + c;
		m.entry[0][1] = t * x * y - s * z;
		m.entry[0][2] = t * x * z + s * y;
		m.entry[1][0] = t * x * y + s * z;
		m.entry[1][1] = t * y * y + c;
		m.entry[1][2] = t * y * z - s * x;
		m.entry[2][0] = t * x * z - s * y;
		m.entry[2][1] = t * y * z + s * x;
		m.entry[2][2] = t * z * z + c;
		return m;
	}

	// shortest rotation that turns direction a_from into direction a_to
	inline NiMatrix3 RotationBetween(const NiPoint3& a_from, const NiPoint3& a_to)
	{
		const NiPoint3 from = Normalized(a_from);
		const NiPoint3 to = Normalized(a_to);
		const float    cosine = std::clamp(from.Dot(to), -1.0f, 1.0f);
		NiPoint3       axis = from.Cross(to);
		if (axis.Length() < 1e-5f) {
			if (cosine > 0.0f) {
				return NiMatrix3{};
			}
			// opposite: any perpendicular axis
			axis = std::abs(from.x) < 0.9f ? from.Cross({ 1.0f, 0.0f, 0.0f }) : from.Cross({ 0.0f, 1.0f, 0.0f });
		}
		return AxisAngle(Normalized(axis), std::acos(cosine));
	}

	// rotation whose local axes a_localA / a_localB (perpendicular) end up along a_worldA / a_worldB
	inline NiMatrix3 AlignAxes(const NiPoint3& a_localA, const NiPoint3& a_localB, const NiPoint3& a_worldA, const NiPoint3& a_worldB)
	{
		const auto frame = [](const NiPoint3& a_a, const NiPoint3& a_b) {
			const NiPoint3 a = Normalized(a_a);
			const NiPoint3 c = Normalized(a.Cross(a_b));
			const NiPoint3 b = c.Cross(a);
			return FromColumns(a, b, c);
		};
		return frame(a_worldA, a_worldB) * frame(a_localA, a_localB).Transpose();
	}

	struct Quat
	{
		float w{ 1.0f }, x{ 0.0f }, y{ 0.0f }, z{ 0.0f };
	};

	inline Quat ToQuat(const NiMatrix3& a_m)
	{
		const auto& e = a_m.entry;
		Quat        q;
		const float trace = e[0][0] + e[1][1] + e[2][2];
		if (trace > 0.0f) {
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			q = { 0.25f * s, (e[2][1] - e[1][2]) / s, (e[0][2] - e[2][0]) / s, (e[1][0] - e[0][1]) / s };
		} else if (e[0][0] > e[1][1] && e[0][0] > e[2][2]) {
			const float s = std::sqrt(1.0f + e[0][0] - e[1][1] - e[2][2]) * 2.0f;
			q = { (e[2][1] - e[1][2]) / s, 0.25f * s, (e[0][1] + e[1][0]) / s, (e[0][2] + e[2][0]) / s };
		} else if (e[1][1] > e[2][2]) {
			const float s = std::sqrt(1.0f + e[1][1] - e[0][0] - e[2][2]) * 2.0f;
			q = { (e[0][2] - e[2][0]) / s, (e[0][1] + e[1][0]) / s, 0.25f * s, (e[1][2] + e[2][1]) / s };
		} else {
			const float s = std::sqrt(1.0f + e[2][2] - e[0][0] - e[1][1]) * 2.0f;
			q = { (e[1][0] - e[0][1]) / s, (e[0][2] + e[2][0]) / s, (e[1][2] + e[2][1]) / s, 0.25f * s };
		}
		return q;
	}

	inline NiMatrix3 ToMatrix(const Quat& a_q)
	{
		const float w = a_q.w, x = a_q.x, y = a_q.y, z = a_q.z;
		NiMatrix3   m;
		m.entry[0][0] = 1.0f - 2.0f * (y * y + z * z);
		m.entry[0][1] = 2.0f * (x * y - z * w);
		m.entry[0][2] = 2.0f * (x * z + y * w);
		m.entry[1][0] = 2.0f * (x * y + z * w);
		m.entry[1][1] = 1.0f - 2.0f * (x * x + z * z);
		m.entry[1][2] = 2.0f * (y * z - x * w);
		m.entry[2][0] = 2.0f * (x * z - y * w);
		m.entry[2][1] = 2.0f * (y * z + x * w);
		m.entry[2][2] = 1.0f - 2.0f * (x * x + y * y);
		return m;
	}

	inline Quat Slerp(Quat a_from, const Quat& a_to, float a_t)
	{
		float cosine = a_from.w * a_to.w + a_from.x * a_to.x + a_from.y * a_to.y + a_from.z * a_to.z;
		if (cosine < 0.0f) {
			a_from = { -a_from.w, -a_from.x, -a_from.y, -a_from.z };
			cosine = -cosine;
		}
		float k0 = 1.0f - a_t;
		float k1 = a_t;
		if (cosine < 0.9995f) {
			const float angle = std::acos(cosine);
			const float s = std::sin(angle);
			k0 = std::sin((1.0f - a_t) * angle) / s;
			k1 = std::sin(a_t * angle) / s;
		}
		Quat q{ k0 * a_from.w + k1 * a_to.w, k0 * a_from.x + k1 * a_to.x, k0 * a_from.y + k1 * a_to.y, k0 * a_from.z + k1 * a_to.z };
		const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (length > 1e-6f) {
			q = { q.w / length, q.x / length, q.y / length, q.z / length };
		}
		return q;
	}

	inline NiMatrix3 Slerp(const NiMatrix3& a_from, const NiMatrix3& a_to, float a_t)
	{
		if (a_t <= 0.0f) {
			return a_from;
		}
		if (a_t >= 1.0f) {
			return a_to;
		}
		return ToMatrix(Slerp(ToQuat(a_from), ToQuat(a_to), a_t));
	}

	// Twist angle of a rotation around a unit axis (swing-twist decomposition)
	inline float TwistAngle(const NiMatrix3& a_rotation, const NiPoint3& a_axis)
	{
		const Quat  q = ToQuat(a_rotation);
		const float projection = q.x * a_axis.x + q.y * a_axis.y + q.z * a_axis.z;
		return 2.0f * std::atan2(projection, q.w);
	}

	// parent * child
	inline NiTransform Combine(const NiTransform& a_parent, const NiTransform& a_child)
	{
		NiTransform result;
		result.rotate = a_parent.rotate * a_child.rotate;
		result.translate = a_parent.translate + a_parent.rotate * a_child.translate * a_parent.scale;
		result.scale = a_parent.scale * a_child.scale;
		return result;
	}

	// the local transform that puts a child of a_parent at a_world
	inline NiTransform ToLocal(const NiTransform& a_parent, const NiTransform& a_world)
	{
		const NiMatrix3 inverse = a_parent.rotate.Transpose();
		const float     scale = a_parent.scale != 0.0f ? a_parent.scale : 1.0f;
		NiTransform     result;
		result.rotate = inverse * a_world.rotate;
		result.translate = inverse * (a_world.translate - a_parent.translate) / scale;
		result.scale = a_world.scale / scale;
		return result;
	}

	inline bool SameRotation(const NiMatrix3& a_lhs, const NiMatrix3& a_rhs)
	{
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				if (std::abs(a_lhs.entry[row][column] - a_rhs.entry[row][column]) > 1e-6f) {
					return false;
				}
			}
		}
		return true;
	}

	// The 1st person camera frame from the player's look angles: x = right, y = forward, z = up
	struct CameraFrame
	{
		NiPoint3  eye;
		NiPoint3  right;
		NiPoint3  forward;
		NiPoint3  up;
		NiMatrix3 basis;  // columns right, forward, up

		NiPoint3 ToWorld(const NiPoint3& a_local) const { return eye + right * a_local.x + forward * a_local.y + up * a_local.z; }
		NiPoint3 Direction(const NiPoint3& a_local) const { return Normalized(right * a_local.x + forward * a_local.y + up * a_local.z); }
	};

	inline CameraFrame MakeCameraFrame(const NiPoint3& a_eye, float a_pitch, float a_yaw)
	{
		CameraFrame frame;
		frame.eye = a_eye;
		frame.forward = { std::sin(a_yaw) * std::cos(a_pitch), std::cos(a_yaw) * std::cos(a_pitch), -std::sin(a_pitch) };
		frame.right = { std::cos(a_yaw), -std::sin(a_yaw), 0.0f };
		frame.up = frame.right.Cross(frame.forward);
		frame.basis = FromColumns(frame.right, frame.forward, frame.up);
		return frame;
	}
}
