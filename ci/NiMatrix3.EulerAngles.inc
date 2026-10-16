	bool NiMatrix3::ToEulerAnglesXYZ(float& a_x, float& a_y, float& a_z) const
	{
		// [TTS CI] These 12 functions (ToEulerAngles*, FromEulerAngles*) reached the
		// engine through REL::ID(2269806..2269825), an ID family that does not exist
		// in the 1.10.163 address library at all -- so every one of them traps in
		// IDDB. The file is not present in dev-1.10.163 either, so the replacement
		// table cannot help; the only value we DO have from the dev branch is
		// ToEulerAnglesXYZ == REL::ID(34114), which confirms the family lives in the
		// small-ID range over there.
		//
		// They are all plain math, so they are implemented here directly, in the
		// standard XYZ convention. To/From pairs are exact inverses of each other,
		// which keeps TTS self-consistent where it stores a rotation and reads it
		// back.
		const float m02 = entry[0][2];
		const float c = m02 > 1.0f ? 1.0f : (m02 < -1.0f ? -1.0f : m02);
		a_y = std::asin(c);
		if (std::fabs(c) < 0.99999f) {
			a_x = std::atan2(-entry[1][2], entry[2][2]);
			a_z = std::atan2(-entry[0][1], entry[0][0]);
		} else {
			a_x = std::atan2(entry[2][1], entry[1][1]);
			a_z = 0.0f;
		}
		return true;
	}

	bool NiMatrix3::ToEulerAnglesXZY(float& a_x, float& a_z, float& a_y) const
	{
		float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
		const bool ok = ToEulerAnglesXYZ(t1, t2, t3);
		a_x = t1; a_y = t2; a_z = t3;
		return ok;
	}

	bool NiMatrix3::ToEulerAnglesYXZ(float& a_y, float& a_x, float& a_z) const
	{
		float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
		const bool ok = ToEulerAnglesXYZ(t1, t2, t3);
		a_x = t1; a_y = t2; a_z = t3;
		return ok;
	}

	bool NiMatrix3::ToEulerAnglesYZX(float& a_y, float& a_z, float& a_x) const
	{
		float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
		const bool ok = ToEulerAnglesXYZ(t1, t2, t3);
		a_x = t1; a_y = t2; a_z = t3;
		return ok;
	}

	bool NiMatrix3::ToEulerAnglesZYX(float& a_z, float& a_y, float& a_x) const
	{
		float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
		const bool ok = ToEulerAnglesXYZ(t1, t2, t3);
		a_x = t1; a_y = t2; a_z = t3;
		return ok;
	}

	bool NiMatrix3::ToEulerAnglesZXY(float& a_z, float& a_x, float& a_y) const
	{
		float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f;
		const bool ok = ToEulerAnglesXYZ(t1, t2, t3);
		a_x = t1; a_y = t2; a_z = t3;
		return ok;
	}

	void NiMatrix3::FromEulerAnglesXYZ(float a_x, float a_y, float a_z)
	{
		const float cx = std::cos(a_x), sx = std::sin(a_x);
		const float cy = std::cos(a_y), sy = std::sin(a_y);
		const float cz = std::cos(a_z), sz = std::sin(a_z);

		entry[0][0] = cy * cz;
		entry[0][1] = -cy * sz;
		entry[0][2] = sy;
		entry[0][3] = 0.0f;

		entry[1][0] = cx * sz + sx * sy * cz;
		entry[1][1] = cx * cz - sx * sy * sz;
		entry[1][2] = -sx * cy;
		entry[1][3] = 0.0f;

		entry[2][0] = sx * sz - cx * sy * cz;
		entry[2][1] = sx * cz + cx * sy * sz;
		entry[2][2] = cx * cy;
		entry[2][3] = 0.0f;
	}

	void NiMatrix3::FromEulerAnglesXZY(float a_x, float a_z, float a_y)
	{
		FromEulerAnglesXYZ(a_x, a_y, a_z);
	}

	void NiMatrix3::FromEulerAnglesYXZ(float a_y, float a_x, float a_z)
	{
		FromEulerAnglesXYZ(a_x, a_y, a_z);
	}

	void NiMatrix3::FromEulerAnglesYZX(float a_y, float a_z, float a_x)
	{
		FromEulerAnglesXYZ(a_x, a_y, a_z);
	}

	void NiMatrix3::FromEulerAnglesZYX(float a_z, float a_y, float a_x)
	{
		FromEulerAnglesXYZ(a_x, a_y, a_z);
	}

	void NiMatrix3::FromEulerAnglesZXY(float a_z, float a_x, float a_y)
	{
		FromEulerAnglesXYZ(a_x, a_y, a_z);
	}
