/*
Minetest
Copyright (C) 2015 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "networkpacket.h"
#include <sstream>
#include "networkexceptions.h"
#include "util/serialize.h"
#include "networkprotocol.h"
#include "serialization.h"
#include "util/encryption.h"

NetworkPacket::NetworkPacket(u16 command, u32 datasize, session_t peer_id):
m_datasize(datasize), m_command(command), m_peer_id(peer_id)
{
	m_data.resize(m_datasize);
}

NetworkPacket::NetworkPacket(u16 command, u32 datasize):
m_datasize(datasize), m_command(command)
{
	m_data.resize(m_datasize);
}

NetworkPacket::~NetworkPacket()
{
	m_data.clear();
}

void NetworkPacket::checkReadOffset(u32 from_offset, u32 field_size)
{
	if (from_offset + field_size > m_datasize) {
		std::stringstream ss;
		ss << "Reading outside packet (offset: " <<
				from_offset << ", packet size: " << getSize() << ")";
		throw PacketError(ss.str());
	}
}

void NetworkPacket::putRawPacket(const u8 *data, u32 datasize, session_t peer_id)
{
	// If a m_command is already set, we are rewriting on same packet
	// This is not permitted
	assert(m_command == 0);

	m_datasize = datasize - 2;
	m_peer_id = peer_id;

	m_data.resize(m_datasize);

	// split command and datas
	m_command = readU16(&data[0]);
	memcpy(m_data.data(), &data[2], m_datasize);
}

void NetworkPacket::clear()
{
	m_data.clear();
	m_datasize = 0;
	m_read_offset = 0;
	m_command = 0;
	m_peer_id = 0;
}

const char* NetworkPacket::getString(u32 from_offset)
{
	checkReadOffset(from_offset, 0);

	return (char*)&m_data[from_offset];
}

void NetworkPacket::putRawString(const char* src, u32 len)
{
	if (m_read_offset + len > m_datasize) {
		m_datasize = m_read_offset + len;
		m_data.resize(m_datasize);
	}

	if (len == 0)
		return;

	memcpy(&m_data[m_read_offset], src, len);
	m_read_offset += len;
}

NetworkPacket& NetworkPacket::operator>>(std::string& dst)
{
	checkReadOffset(m_read_offset, 2);
	u16 strLen = readU16(&m_data[m_read_offset]);
	m_read_offset += 2;

	dst.clear();

	if (strLen == 0) {
		return *this;
	}

	checkReadOffset(m_read_offset, strLen);

	dst.reserve(strLen);
	dst.append((char*)&m_data[m_read_offset], strLen);

	m_read_offset += strLen;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(const std::string &src)
{
	if (src.size() > STRING_MAX_LEN) {
		throw PacketError("String too long");
	}

	u16 msgsize = src.size();

	*this << msgsize;

	putRawString(src.c_str(), (u32)msgsize);

	return *this;
}

void NetworkPacket::putLongString(const std::string &src)
{
	if (src.size() > LONG_STRING_MAX_LEN) {
		throw PacketError("String too long");
	}

	u32 msgsize = src.size();

	*this << msgsize;

	putRawString(src.c_str(), msgsize);
}

static constexpr bool NEED_SURROGATE_CODING = sizeof(wchar_t) > 2;

NetworkPacket& NetworkPacket::operator>>(std::wstring& dst)
{
	checkReadOffset(m_read_offset, 2);
	u16 strLen = readU16(&m_data[m_read_offset]);
	m_read_offset += 2;

	dst.clear();

	if (strLen == 0) {
		return *this;
	}

	checkReadOffset(m_read_offset, strLen * 2);

	dst.reserve(strLen);
	for (u16 i = 0; i < strLen; i++) {
		wchar_t c = readU16(&m_data[m_read_offset]);
		if (NEED_SURROGATE_CODING && c >= 0xD800 && c < 0xDC00 && i+1 < strLen) {
			i++;
			m_read_offset += sizeof(u16);

			wchar_t c2 = readU16(&m_data[m_read_offset]);
			c = 0x10000 + ( ((c & 0x3ff) << 10) | (c2 & 0x3ff) );
		}
		dst.push_back(c);
		m_read_offset += sizeof(u16);
	}

	return *this;
}

NetworkPacket& NetworkPacket::operator<<(const std::wstring &src)
{
	if (src.size() > WIDE_STRING_MAX_LEN) {
		throw PacketError("String too long");
	}

	if (!NEED_SURROGATE_CODING || src.size() == 0) {
		*this << static_cast<u16>(src.size());
		for (u16 i = 0; i < src.size(); i++)
			*this << static_cast<u16>(src[i]);

		return *this;
	}

	// write dummy value, to be overwritten later
	const u32 len_offset = m_read_offset;
	u32 written = 0;
	*this << static_cast<u16>(0xfff0);

	for (u16 i = 0; i < src.size(); i++) {
		wchar_t c = src[i];
		if (c > 0xffff) {
			// Encode high code-points as surrogate pairs
			u32 n = c - 0x10000;
			*this << static_cast<u16>(0xD800 | (n >> 10))
				<< static_cast<u16>(0xDC00 | (n & 0x3ff));
			written += 2;
		} else {
			*this << static_cast<u16>(c);
			written++;
		}
	}

	if (written > WIDE_STRING_MAX_LEN)
		throw PacketError("String too long");
	writeU16(&m_data[len_offset], written);

	return *this;
}

std::string NetworkPacket::readLongString()
{
	checkReadOffset(m_read_offset, 4);
	u32 strLen = readU32(&m_data[m_read_offset]);
	m_read_offset += 4;

	if (strLen == 0) {
		return "";
	}

	if (strLen > LONG_STRING_MAX_LEN) {
		throw PacketError("String too long");
	}

	checkReadOffset(m_read_offset, strLen);

	std::string dst;

	dst.reserve(strLen);
	dst.append((char*)&m_data[m_read_offset], strLen);

	m_read_offset += strLen;

	return dst;
}

NetworkPacket& NetworkPacket::operator>>(char& dst)
{
	checkReadOffset(m_read_offset, 1);

	dst = readU8(&m_data[m_read_offset]);

	m_read_offset += 1;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(char src)
{
	checkDataSize(1);

	writeU8(&m_data[m_read_offset], src);

	m_read_offset += 1;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(u8 src)
{
	checkDataSize(1);

	writeU8(&m_data[m_read_offset], src);

	m_read_offset += 1;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(bool src)
{
	checkDataSize(1);

	writeU8(&m_data[m_read_offset], src);

	m_read_offset += 1;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(u16 src)
{
	checkDataSize(2);

	writeU16(&m_data[m_read_offset], src);

	m_read_offset += 2;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(u32 src)
{
	checkDataSize(4);

	writeU32(&m_data[m_read_offset], src);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(u64 src)
{
	checkDataSize(8);

	writeU64(&m_data[m_read_offset], src);

	m_read_offset += 8;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(float src)
{
	checkDataSize(4);

	writeF32(&m_data[m_read_offset], src);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(bool& dst)
{
	checkReadOffset(m_read_offset, 1);

	dst = readU8(&m_data[m_read_offset]);

	m_read_offset += 1;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(u8& dst)
{
	checkReadOffset(m_read_offset, 1);

	dst = readU8(&m_data[m_read_offset]);

	m_read_offset += 1;
	return *this;
}

u8 NetworkPacket::getU8(u32 offset)
{
	checkReadOffset(offset, 1);

	return readU8(&m_data[offset]);
}

u8* NetworkPacket::getU8Ptr(u32 from_offset)
{
	if (m_datasize == 0) {
		return NULL;
	}

	checkReadOffset(from_offset, 1);

	return (u8*)&m_data[from_offset];
}

NetworkPacket& NetworkPacket::operator>>(u16& dst)
{
	checkReadOffset(m_read_offset, 2);

	dst = readU16(&m_data[m_read_offset]);

	m_read_offset += 2;
	return *this;
}

u16 NetworkPacket::getU16(u32 from_offset)
{
	checkReadOffset(from_offset, 2);

	return readU16(&m_data[from_offset]);
}

NetworkPacket& NetworkPacket::operator>>(u32& dst)
{
	checkReadOffset(m_read_offset, 4);

	dst = readU32(&m_data[m_read_offset]);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(u64& dst)
{
	checkReadOffset(m_read_offset, 8);

	dst = readU64(&m_data[m_read_offset]);

	m_read_offset += 8;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(float& dst)
{
	checkReadOffset(m_read_offset, 4);

	dst = readF32(&m_data[m_read_offset]);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(v2f& dst)
{
	checkReadOffset(m_read_offset, 8);

	dst = readV2F32(&m_data[m_read_offset]);

	m_read_offset += 8;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(v3f& dst)
{
	checkReadOffset(m_read_offset, 12);

	dst = readV3F32(&m_data[m_read_offset]);

	m_read_offset += 12;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(s16& dst)
{
	checkReadOffset(m_read_offset, 2);

	dst = readS16(&m_data[m_read_offset]);

	m_read_offset += 2;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(s16 src)
{
	*this << (u16) src;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(s32& dst)
{
	checkReadOffset(m_read_offset, 4);

	dst = readS32(&m_data[m_read_offset]);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(s32 src)
{
	*this << (u32) src;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(v3s16& dst)
{
	checkReadOffset(m_read_offset, 6);

	dst = readV3S16(&m_data[m_read_offset]);

	m_read_offset += 6;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(v2s32& dst)
{
	checkReadOffset(m_read_offset, 8);

	dst = readV2S32(&m_data[m_read_offset]);

	m_read_offset += 8;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(v3s32& dst)
{
	checkReadOffset(m_read_offset, 12);

	dst = readV3S32(&m_data[m_read_offset]);

	m_read_offset += 12;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(v2f src)
{
	*this << (float) src.X;
	*this << (float) src.Y;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(v3f src)
{
	*this << (float) src.X;
	*this << (float) src.Y;
	*this << (float) src.Z;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(v3s16 src)
{
	*this << (s16) src.X;
	*this << (s16) src.Y;
	*this << (s16) src.Z;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(v2s32 src)
{
	*this << (s32) src.X;
	*this << (s32) src.Y;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(v3s32 src)
{
	*this << (s32) src.X;
	*this << (s32) src.Y;
	*this << (s32) src.Z;
	return *this;
}

NetworkPacket& NetworkPacket::operator>>(video::SColor& dst)
{
	checkReadOffset(m_read_offset, 4);

	dst = readARGB8(&m_data[m_read_offset]);

	m_read_offset += 4;
	return *this;
}

NetworkPacket& NetworkPacket::operator<<(video::SColor src)
{
	checkDataSize(4);

	writeU32(&m_data[m_read_offset], src.color);

	m_read_offset += 4;
	return *this;
}

Buffer<u8> NetworkPacket::oldForgePacket()
{
	Buffer<u8> sb(m_datasize + 2);
	writeU16(&sb[0], m_command);
	memcpy(&sb[2], m_data.data(), m_datasize);

	return sb;
}

#include <map>

std::map<int, int> encrypt_decompressed;
std::map<int, int> encrypt_compressed;
std::map<int, int> decrypt_decompressed;
std::map<int, int> decrypt_compressed;

std::string toserver_command_to_text(int command)
{
	if (command == TOSERVER_INIT)
		return "TOSERVER_INIT";
	else if (command == TOSERVER_INIT_LEGACY)
		return "TOSERVER_INIT_LEGACY";
	else if (command == TOSERVER_INIT2)
		return "TOSERVER_INIT2";
	else if (command == TOSERVER_MODCHANNEL_JOIN)
		return "TOSERVER_MODCHANNEL_JOIN";
	else if (command == TOSERVER_MODCHANNEL_LEAVE)
		return "TOSERVER_MODCHANNEL_LEAVE";
	else if (command == TOSERVER_MODCHANNEL_MSG)
		return "TOSERVER_MODCHANNEL_MSG";
	else if (command == TOSERVER_GETBLOCK)
		return "TOSERVER_GETBLOCK";
	else if (command == TOSERVER_ADDNODE)
		return "TOSERVER_ADDNODE";
	else if (command == TOSERVER_REMOVENODE)
		return "TOSERVER_REMOVENODE";
	else if (command == TOSERVER_PLAYERPOS)
		return "TOSERVER_PLAYERPOS";
	else if (command == TOSERVER_GOTBLOCKS)
		return "TOSERVER_GOTBLOCKS";
	else if (command == TOSERVER_DELETEDBLOCKS)
		return "TOSERVER_DELETEDBLOCKS";
	else if (command == TOSERVER_ADDNODE_FROM_INVENTORY)
		return "TOSERVER_ADDNODE_FROM_INVENTORY";
	else if (command == TOSERVER_CLICK_OBJECT)
		return "TOSERVER_CLICK_OBJECT";
	else if (command == TOSERVER_GROUND_ACTION)
		return "TOSERVER_GROUND_ACTION";
	else if (command == TOSERVER_RELEASE)
		return "TOSERVER_RELEASE";
	else if (command == TOSERVER_SIGNTEXT)
		return "TOSERVER_SIGNTEXT";
	else if (command == TOSERVER_INVENTORY_ACTION)
		return "TOSERVER_INVENTORY_ACTION";
	else if (command == TOSERVER_CHAT_MESSAGE)
		return "TOSERVER_CHAT_MESSAGE";
	else if (command == TOSERVER_SIGNNODETEXT)
		return "TOSERVER_SIGNNODETEXT";
	else if (command == TOSERVER_CLICK_ACTIVEOBJECT)
		return "TOSERVER_CLICK_ACTIVEOBJECT";
	else if (command == TOSERVER_DAMAGE)
		return "TOSERVER_DAMAGE";
	else if (command == TOSERVER_PASSWORD_LEGACY)
		return "TOSERVER_PASSWORD_LEGACY";
	else if (command == TOSERVER_PLAYERITEM)
		return "TOSERVER_PLAYERITEM";
	else if (command == TOSERVER_RESPAWN)
		return "TOSERVER_RESPAWN";
	else if (command == TOSERVER_INTERACT)
		return "TOSERVER_INTERACT";
	else if (command == TOSERVER_REMOVED_SOUNDS)
		return "TOSERVER_REMOVED_SOUNDS";
	else if (command == TOSERVER_NODEMETA_FIELDS)
		return "TOSERVER_NODEMETA_FIELDS";
	else if (command == TOSERVER_INVENTORY_FIELDS)
		return "TOSERVER_INVENTORY_FIELDS";
	else if (command == TOSERVER_REQUEST_MEDIA)
		return "TOSERVER_REQUEST_MEDIA";
	else if (command == TOSERVER_RECEIVED_MEDIA)
		return "TOSERVER_RECEIVED_MEDIA";
	else if (command == TOSERVER_BREATH)
		return "TOSERVER_BREATH";
	else if (command == TOSERVER_CLIENT_READY)
		return "TOSERVER_CLIENT_READY";
	else if (command == TOSERVER_FIRST_SRP)
		return "TOSERVER_FIRST_SRP";
	else if (command == TOSERVER_SRP_BYTES_A)
		return "TOSERVER_SRP_BYTES_A";
	else if (command == TOSERVER_SRP_BYTES_M)
		return "TOSERVER_SRP_BYTES_M";
	else if (command == TOSERVER_NUM_MSG_TYPES)
		return "TOSERVER_NUM_MSG_TYPES";
	else
		return "unknown";
}

std::string toclient_command_to_text(int command)
{
	if (command == TOCLIENT_HELLO)
		return "TOCLIENT_HELLO";
	else if (command == TOCLIENT_AUTH_ACCEPT)
		return "TOCLIENT_AUTH_ACCEPT";
	else if (command == TOCLIENT_ACCEPT_SUDO_MODE)
		return "TOCLIENT_ACCEPT_SUDO_MODE";
	else if (command == TOCLIENT_DENY_SUDO_MODE)
		return "TOCLIENT_DENY_SUDO_MODE";
	else if (command == TOCLIENT_ACCESS_DENIED)
		return "TOCLIENT_ACCESS_DENIED";
	else if (command == TOCLIENT_INIT_LEGACY)
		return "TOCLIENT_INIT_LEGACY";
	else if (command == TOCLIENT_BLOCKDATA)
		return "TOCLIENT_BLOCKDATA";
	else if (command == TOCLIENT_ADDNODE)
		return "TOCLIENT_ADDNODE";
	else if (command == TOCLIENT_REMOVENODE)
		return "TOCLIENT_REMOVENODE";
	else if (command == TOCLIENT_PLAYERPOS)
		return "TOCLIENT_PLAYERPOS";
	else if (command == TOCLIENT_PLAYERINFO)
		return "TOCLIENT_PLAYERINFO";
	else if (command == TOCLIENT_OPT_BLOCK_NOT_FOUND)
		return "TOCLIENT_OPT_BLOCK_NOT_FOUND";
	else if (command == TOCLIENT_SECTORMETA)
		return "TOCLIENT_SECTORMETA";
	else if (command == TOCLIENT_INVENTORY)
		return "TOCLIENT_INVENTORY";
	else if (command == TOCLIENT_OBJECTDATA)
		return "TOCLIENT_OBJECTDATA";
	else if (command == TOCLIENT_TIME_OF_DAY)
		return "TOCLIENT_TIME_OF_DAY";
	else if (command == TOCLIENT_CSM_RESTRICTION_FLAGS)
		return "TOCLIENT_CSM_RESTRICTION_FLAGS";
	else if (command == TOCLIENT_PLAYER_SPEED)
		return "TOCLIENT_PLAYER_SPEED";
	else if (command == TOCLIENT_MEDIA_PUSH)
		return "TOCLIENT_MEDIA_PUSH";
	else if (command == TOCLIENT_COPY_TO_CLIPBOARD)
		return "TOCLIENT_COPY_TO_CLIPBOARD";
	else if (command == TOCLIENT_CHAT_MESSAGE)
		return "TOCLIENT_CHAT_MESSAGE";
	else if (command == TOCLIENT_CHAT_MESSAGE_OLD)
		return "TOCLIENT_CHAT_MESSAGE_OLD";
	else if (command == TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD)
		return "TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD";
	else if (command == TOCLIENT_ACTIVE_OBJECT_MESSAGES)
		return "TOCLIENT_ACTIVE_OBJECT_MESSAGES";
	else if (command == TOCLIENT_HP)
		return "TOCLIENT_HP";
	else if (command == TOCLIENT_MOVE_PLAYER)
		return "TOCLIENT_MOVE_PLAYER";
	else if (command == TOCLIENT_ACCESS_DENIED_LEGACY)
		return "TOCLIENT_ACCESS_DENIED_LEGACY";
	else if (command == TOCLIENT_FOV)
		return "TOCLIENT_FOV";
	else if (command == TOCLIENT_DEATHSCREEN)
		return "TOCLIENT_DEATHSCREEN";
	else if (command == TOCLIENT_MEDIA)
		return "TOCLIENT_MEDIA";
	else if (command == TOCLIENT_TOOLDEF)
		return "TOCLIENT_TOOLDEF";
	else if (command == TOCLIENT_NODEDEF)
		return "TOCLIENT_NODEDEF";
	else if (command == TOCLIENT_CRAFTITEMDEF)
		return "TOCLIENT_CRAFTITEMDEF";
	else if (command == TOCLIENT_ANNOUNCE_MEDIA)
		return "TOCLIENT_ANNOUNCE_MEDIA";
	else if (command == TOCLIENT_ITEMDEF)
		return "TOCLIENT_ITEMDEF";
	else if (command == TOCLIENT_PLAY_SOUND)
		return "TOCLIENT_PLAY_SOUND";
	else if (command == TOCLIENT_STOP_SOUND)
		return "TOCLIENT_STOP_SOUND";
	else if (command == TOCLIENT_PRIVILEGES)
		return "TOCLIENT_PRIVILEGES";
	else if (command == TOCLIENT_INVENTORY_FORMSPEC)
		return "TOCLIENT_INVENTORY_FORMSPEC";
	else if (command == TOCLIENT_DETACHED_INVENTORY)
		return "TOCLIENT_DETACHED_INVENTORY";
	else if (command == TOCLIENT_SHOW_FORMSPEC)
		return "TOCLIENT_SHOW_FORMSPEC";
	else if (command == TOCLIENT_MOVEMENT)
		return "TOCLIENT_MOVEMENT";
	else if (command == TOCLIENT_SPAWN_PARTICLE)
		return "TOCLIENT_SPAWN_PARTICLE";
	else if (command == TOCLIENT_ADD_PARTICLESPAWNER)
		return "TOCLIENT_ADD_PARTICLESPAWNER";
	else if (command == TOCLIENT_DELETE_PARTICLESPAWNER_LEGACY)
		return "TOCLIENT_DELETE_PARTICLESPAWNER_LEGACY";
	else if (command == TOCLIENT_HUDADD)
		return "TOCLIENT_HUDADD";
	else if (command == TOCLIENT_HUDRM)
		return "TOCLIENT_HUDRM";
	else if (command == TOCLIENT_HUDCHANGE)
		return "TOCLIENT_HUDCHANGE";
	else if (command == TOCLIENT_HUD_SET_FLAGS)
		return "TOCLIENT_HUD_SET_FLAGS";
	else if (command == TOCLIENT_HUD_SET_PARAM)
		return "TOCLIENT_HUD_SET_PARAM";
	else if (command == TOCLIENT_BREATH)
		return "TOCLIENT_BREATH";
	else if (command == TOCLIENT_SET_SKY)
		return "TOCLIENT_SET_SKY";
	else if (command == TOCLIENT_OVERRIDE_DAY_NIGHT_RATIO)
		return "TOCLIENT_OVERRIDE_DAY_NIGHT_RATIO";
	else if (command == TOCLIENT_LOCAL_PLAYER_ANIMATIONS)
		return "TOCLIENT_LOCAL_PLAYER_ANIMATIONS";
	else if (command == TOCLIENT_EYE_OFFSET)
		return "TOCLIENT_EYE_OFFSET";
	else if (command == TOCLIENT_DELETE_PARTICLESPAWNER)
		return "TOCLIENT_DELETE_PARTICLESPAWNER";
	else if (command == TOCLIENT_CLOUD_PARAMS)
		return "TOCLIENT_CLOUD_PARAMS";
	else if (command == TOCLIENT_FADE_SOUND)
		return "TOCLIENT_FADE_SOUND";
	else if (command == TOCLIENT_UPDATE_PLAYER_LIST)
		return "TOCLIENT_UPDATE_PLAYER_LIST";
	else if (command == TOCLIENT_MODCHANNEL_MSG)
		return "TOCLIENT_MODCHANNEL_MSG";
	else if (command == TOCLIENT_MODCHANNEL_SIGNAL)
		return "TOCLIENT_MODCHANNEL_SIGNAL";
	else if (command == TOCLIENT_NODEMETA_CHANGED)
		return "TOCLIENT_NODEMETA_CHANGED";
	else if (command == TOCLIENT_SET_SUN)
		return "TOCLIENT_SET_SUN";
	else if (command == TOCLIENT_SET_MOON)
		return "TOCLIENT_SET_MOON";
	else if (command == TOCLIENT_SET_STARS)
		return "TOCLIENT_SET_STARS";
	else if (command == TOCLIENT_SRP_BYTES_S_B)
		return "TOCLIENT_SRP_BYTES_S_B";
	else if (command == TOCLIENT_FORMSPEC_PREPEND)
		return "TOCLIENT_FORMSPEC_PREPEND";
	else if (command == TOCLIENT_MINIMAP_MODES)
		return "TOCLIENT_MINIMAP_MODES";
	else if (command == TOCLIENT_NUM_MSG_TYPES)
		return "TOCLIENT_NUM_MSG_TYPES";
	else
		return "unknown";
}

void print_results()
{
	errorstream << std::endl;
	errorstream << "encrypt" << std::endl;
	
	for (auto const& x : encrypt_compressed)
	{
		int command = x.first;
		int decompressed = encrypt_decompressed[command];
		
		if (decompressed == 0)
			continue;
			
		int compressed = encrypt_compressed[command];
		int diff = (decompressed - compressed) * 100 / decompressed;
		errorstream << "  command: " << command << std::endl;
		errorstream << "  command name: " << toserver_command_to_text(command) << std::endl;
		errorstream << "  decompressed: " << decompressed << std::endl;
		errorstream << "  compressed: " << compressed << std::endl;
		errorstream << "  diff: " << diff << "%" << std::endl;
		errorstream << std::endl;
	}
	
	errorstream << std::endl;
	errorstream << "decrypt" << std::endl;

	for (auto const& x : decrypt_compressed)
	{
		int command = x.first;
		int decompressed = decrypt_decompressed[command];
		int compressed = decrypt_compressed[command];
		
		if (decompressed == 0)
			continue;
			
		int diff = (decompressed - compressed) * 100 / decompressed;
		errorstream << "  command: " << command << std::endl;
		errorstream << "  command name: " << toclient_command_to_text(command) << std::endl;
		errorstream << "  decompressed: " << decompressed << std::endl;
		errorstream << "  compressed: " << compressed << std::endl;
		errorstream << "  diff: " << diff << "%" << std::endl;
		errorstream << std::endl;
	}
}

bool NetworkPacket::encrypt(std::string key)
{
	std::string data((const char*)(m_data.data()), m_datasize);
	
	//~ if (m_datasize >= 256 && key == "client")
		//~ errorstream << "encrypt command " << toserver_command_to_text(m_command) << ", size " << m_datasize << std::endl;
	
	if (encrypt_decompressed.find(m_command) == encrypt_decompressed.end())
		encrypt_decompressed[m_command] = 0;
	if (encrypt_compressed.find(m_command) == encrypt_compressed.end())
		encrypt_compressed[m_command] = 0;
		
	encrypt_decompressed[m_command] += data.size();
	//errorstream << "encrypt data1 " << data.size() << std::endl;
	
	std::ostringstream os_compressed(std::ios::binary);
	compressZlib(data, os_compressed, 9);
	//compressZstd(data, os_compressed, 22);
	
	std::string data_to_write = os_compressed.str();
	
	encrypt_compressed[m_command] += data_to_write.size();
	//errorstream << "encrypt data2 " << data_to_write.size() << std::endl;
	
	if (key == "client")
		print_results();
	
	m_read_offset = 0;
	m_datasize = data_to_write.size();
	m_data.resize(m_datasize);
	memcpy(&m_data[0], data_to_write.c_str(), m_datasize);
	

	return true;
}

bool NetworkPacket::decrypt(std::string key)
{
	std::string data((const char*)(m_data.data()), m_datasize);
	
	if (decrypt_decompressed.find(m_command) == decrypt_decompressed.end())
		decrypt_decompressed[m_command] = 0;
	if (decrypt_compressed.find(m_command) == decrypt_compressed.end())
		decrypt_compressed[m_command] = 0;

	decrypt_compressed[m_command] += data.size();
	//errorstream << "decrypt data1 " << data.size() << std::endl;
		
	std::istringstream is_compressed(data, std::ios::binary);
	std::ostringstream os_decompressed(std::ios::binary);
    try {
        decompressZlib(is_compressed, os_decompressed);
        //decompressZstd(is_compressed, os_decompressed);
    } catch (const SerializationError& e) {
        errorstream << "Decompression failed: " << e.what() << std::endl;
        return false;
    }
	
	std::string data_to_write = os_decompressed.str();
	
	decrypt_decompressed[m_command] += data_to_write.size();
	//errorstream << "decrypt data2 " << data_to_write.size() << std::endl;

	if (key == "client")
		print_results();
		
	//~ if (data_to_write.size() >= 256 && key == "client" && m_command != TOCLIENT_BLOCKDATA)
		//~ errorstream << "decrypt command " << toclient_command_to_text(m_command) << ", size " << data_to_write.size() << std::endl;

	m_read_offset = 0;
	m_datasize = data_to_write.size();
	m_data.resize(m_datasize);
	memcpy(&m_data[0], data_to_write.c_str(), m_datasize);

	return true;
}
