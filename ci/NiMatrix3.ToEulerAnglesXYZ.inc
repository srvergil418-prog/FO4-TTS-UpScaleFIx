	bool NiMatrix3::ToEulerAnglesXYZ(float& a_x, float& a_y, float& a_z) const
	{
		// [TTS CI] The engine implementation was reached through REL::ID(2269806),
		// which does not exist in the 1.10.163 address library -- that whole ID
		// family (2269806..2269825) is absent from it, and the dev-1.10.163 branch
		// does not even ship NiMatrix3.cpp, so the replacement table cannot supply
		// a value. Calling through it trapped in IDDB and killed the process; the
		// observed symptom was "id 2269806 not found!" on opening the Debug tab
		// (TTS calls this from 4 places in DebugPanel.cpp).
		//
		// It is plain math, so compute the XYZ Euler angles from the rotation
		// matrix directly. The 11 sibling functions in this file
		// (ToEulerAnglesXZY/YXZ/YZX/ZYX and the MakeRotation* family) are left
		// alone on purpose: function-local statics resolve lazily, TTS never
		// calls them, so they are never resolved and cannot trap.
		const float m02 = entry[0][2];
		const float clamped = m02 > 1.0f ? 1.0f : (m02 < -1.0f ? -1.0f : m02);
		a_y = std::asin(clamped);
		if (std::fabs(clamped) < 0.99999f) {
			a_x = std::atan2(-entry[1][2], entry[2][2]);
			a_z = std::atan2(-entry[0][1], entry[0][0]);
		} else {
			a_x = std::atan2(entry[2][1], entry[1][1]);
			a_z = 0.0f;
		}
		return true;
	}
