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
		errorstream << "  decompressed: " << decompressed << std::endl;
		errorstream << "  compressed: " << compressed << std::endl;
		errorstream << "  diff: " << diff << "%" << std::endl;
		
		if (command == TOSERVER_INIT)
			errorstream << "  command name: TOSERVER_INIT" << std::endl;
		else if (command == TOSERVER_INIT_LEGACY)
			errorstream << "  command name: TOSERVER_INIT_LEGACY" << std::endl;
		else if (command == TOSERVER_INIT2)
			errorstream << "  command name: TOSERVER_INIT2" << std::endl;
		else if (command == TOSERVER_MODCHANNEL_JOIN)
			errorstream << "  command name: TOSERVER_MODCHANNEL_JOIN" << std::endl;
		else if (command == TOSERVER_MODCHANNEL_LEAVE)
			errorstream << "  command name: TOSERVER_MODCHANNEL_LEAVE" << std::endl;
		else if (command == TOSERVER_MODCHANNEL_MSG)
			errorstream << "  command name: TOSERVER_MODCHANNEL_MSG" << std::endl;
		else if (command == TOSERVER_GETBLOCK)
			errorstream << "  command name: TOSERVER_GETBLOCK" << std::endl;
		else if (command == TOSERVER_ADDNODE)
			errorstream << "  command name: TOSERVER_ADDNODE" << std::endl;
		else if (command == TOSERVER_REMOVENODE)
			errorstream << "  command name: TOSERVER_REMOVENODE" << std::endl;
		else if (command == TOSERVER_PLAYERPOS)
			errorstream << "  command name: TOSERVER_PLAYERPOS" << std::endl;
		else if (command == TOSERVER_GOTBLOCKS)
			errorstream << "  command name: TOSERVER_GOTBLOCKS" << std::endl;
		else if (command == TOSERVER_DELETEDBLOCKS)
			errorstream << "  command name: TOSERVER_DELETEDBLOCKS" << std::endl;
		else if (command == TOSERVER_ADDNODE_FROM_INVENTORY)
			errorstream << "  command name: TOSERVER_ADDNODE_FROM_INVENTORY" << std::endl;
		else if (command == TOSERVER_CLICK_OBJECT)
			errorstream << "  command name: TOSERVER_CLICK_OBJECT" << std::endl;
		else if (command == TOSERVER_GROUND_ACTION)
			errorstream << "  command name: TOSERVER_GROUND_ACTION" << std::endl;
		else if (command == TOSERVER_RELEASE)
			errorstream << "  command name: TOSERVER_RELEASE" << std::endl;
		else if (command == TOSERVER_SIGNTEXT)
			errorstream << "  command name: TOSERVER_SIGNTEXT" << std::endl;
		else if (command == TOSERVER_INVENTORY_ACTION)
			errorstream << "  command name: TOSERVER_INVENTORY_ACTION" << std::endl;
		else if (command == TOSERVER_CHAT_MESSAGE)
			errorstream << "  command name: TOSERVER_CHAT_MESSAGE" << std::endl;
		else if (command == TOSERVER_SIGNNODETEXT)
			errorstream << "  command name: TOSERVER_SIGNNODETEXT" << std::endl;
		else if (command == TOSERVER_CLICK_ACTIVEOBJECT)
			errorstream << "  command name: TOSERVER_CLICK_ACTIVEOBJECT" << std::endl;
		else if (command == TOSERVER_DAMAGE)
			errorstream << "  command name: TOSERVER_DAMAGE" << std::endl;
		else if (command == TOSERVER_PASSWORD_LEGACY)
			errorstream << "  command name: TOSERVER_PASSWORD_LEGACY" << std::endl;
		else if (command == TOSERVER_PLAYERITEM)
			errorstream << "  command name: TOSERVER_PLAYERITEM" << std::endl;
		else if (command == TOSERVER_RESPAWN)
			errorstream << "  command name: TOSERVER_RESPAWN" << std::endl;
		else if (command == TOSERVER_INTERACT)
			errorstream << "  command name: TOSERVER_INTERACT" << std::endl;
		else if (command == TOSERVER_REMOVED_SOUNDS)
			errorstream << "  command name: TOSERVER_REMOVED_SOUNDS" << std::endl;
		else if (command == TOSERVER_NODEMETA_FIELDS)
			errorstream << "  command name: TOSERVER_NODEMETA_FIELDS" << std::endl;
		else if (command == TOSERVER_INVENTORY_FIELDS)
			errorstream << "  command name: TOSERVER_INVENTORY_FIELDS" << std::endl;
		else if (command == TOSERVER_REQUEST_MEDIA)
			errorstream << "  command name: TOSERVER_REQUEST_MEDIA" << std::endl;
		else if (command == TOSERVER_RECEIVED_MEDIA)
			errorstream << "  command name: TOSERVER_RECEIVED_MEDIA" << std::endl;
		else if (command == TOSERVER_BREATH)
			errorstream << "  command name: TOSERVER_BREATH" << std::endl;
		else if (command == TOSERVER_CLIENT_READY)
			errorstream << "  command name: TOSERVER_CLIENT_READY" << std::endl;
		else if (command == TOSERVER_FIRST_SRP)
			errorstream << "  command name: TOSERVER_FIRST_SRP" << std::endl;
		else if (command == TOSERVER_SRP_BYTES_A)
			errorstream << "  command name: TOSERVER_SRP_BYTES_A" << std::endl;
		else if (command == TOSERVER_SRP_BYTES_M)
			errorstream << "  command name: TOSERVER_SRP_BYTES_M" << std::endl;
		else if (command == TOSERVER_NUM_MSG_TYPES)
			errorstream << "  command name: TOSERVER_NUM_MSG_TYPES" << std::endl;
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
		errorstream << "  decompressed: " << decompressed << std::endl;
		errorstream << "  compressed: " << compressed << std::endl;
		errorstream << "  diff: " << diff << "%" << std::endl;
		
		if (command == TOCLIENT_HELLO)
			errorstream << "  command name: TOCLIENT_HELLO" << std::endl;
		else if (command == TOCLIENT_AUTH_ACCEPT)
			errorstream << "  command name: TOCLIENT_AUTH_ACCEPT" << std::endl;
		else if (command == TOCLIENT_ACCEPT_SUDO_MODE)
			errorstream << "  command name: TOCLIENT_ACCEPT_SUDO_MODE" << std::endl;
		else if (command == TOCLIENT_DENY_SUDO_MODE)
			errorstream << "  command name: TOCLIENT_DENY_SUDO_MODE" << std::endl;
		else if (command == TOCLIENT_ACCESS_DENIED)
			errorstream << "  command name: TOCLIENT_ACCESS_DENIED" << std::endl;
		else if (command == TOCLIENT_INIT_LEGACY)
			errorstream << "  command name: TOCLIENT_INIT_LEGACY" << std::endl;
		else if (command == TOCLIENT_BLOCKDATA)
			errorstream << "  command name: TOCLIENT_BLOCKDATA" << std::endl;
		else if (command == TOCLIENT_ADDNODE)
			errorstream << "  command name: TOCLIENT_ADDNODE" << std::endl;
		else if (command == TOCLIENT_REMOVENODE)
			errorstream << "  command name: TOCLIENT_REMOVENODE" << std::endl;
		else if (command == TOCLIENT_PLAYERPOS)
			errorstream << "  command name: TOCLIENT_PLAYERPOS" << std::endl;
		else if (command == TOCLIENT_PLAYERINFO)
			errorstream << "  command name: TOCLIENT_PLAYERINFO" << std::endl;
		else if (command == TOCLIENT_OPT_BLOCK_NOT_FOUND)
			errorstream << "  command name: TOCLIENT_OPT_BLOCK_NOT_FOUND" << std::endl;
		else if (command == TOCLIENT_SECTORMETA)
			errorstream << "  command name: TOCLIENT_SECTORMETA" << std::endl;
		else if (command == TOCLIENT_INVENTORY)
			errorstream << "  command name: TOCLIENT_INVENTORY" << std::endl;
		else if (command == TOCLIENT_OBJECTDATA)
			errorstream << "  command name: TOCLIENT_OBJECTDATA" << std::endl;
		else if (command == TOCLIENT_TIME_OF_DAY)
			errorstream << "  command name: TOCLIENT_TIME_OF_DAY" << std::endl;
		else if (command == TOCLIENT_CSM_RESTRICTION_FLAGS)
			errorstream << "  command name: TOCLIENT_CSM_RESTRICTION_FLAGS" << std::endl;
		else if (command == TOCLIENT_PLAYER_SPEED)
			errorstream << "  command name: TOCLIENT_PLAYER_SPEED" << std::endl;
		else if (command == TOCLIENT_MEDIA_PUSH)
			errorstream << "  command name: TOCLIENT_MEDIA_PUSH" << std::endl;
		else if (command == TOCLIENT_COPY_TO_CLIPBOARD)
			errorstream << "  command name: TOCLIENT_COPY_TO_CLIPBOARD" << std::endl;
		else if (command == TOCLIENT_CHAT_MESSAGE)
			errorstream << "  command name: TOCLIENT_CHAT_MESSAGE" << std::endl;
		else if (command == TOCLIENT_CHAT_MESSAGE_OLD)
			errorstream << "  command name: TOCLIENT_CHAT_MESSAGE_OLD" << std::endl;
		else if (command == TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD)
			errorstream << "  command name: TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD" << std::endl;
		else if (command == TOCLIENT_ACTIVE_OBJECT_MESSAGES)
			errorstream << "  command name: TOCLIENT_ACTIVE_OBJECT_MESSAGES" << std::endl;
		else if (command == TOCLIENT_HP)
			errorstream << "  command name: TOCLIENT_HP" << std::endl;
		else if (command == TOCLIENT_MOVE_PLAYER)
			errorstream << "  command name: TOCLIENT_MOVE_PLAYER" << std::endl;
		else if (command == TOCLIENT_ACCESS_DENIED_LEGACY)
			errorstream << "  command name: TOCLIENT_ACCESS_DENIED_LEGACY" << std::endl;
		else if (command == TOCLIENT_FOV)
			errorstream << "  command name: TOCLIENT_FOV" << std::endl;
		else if (command == TOCLIENT_DEATHSCREEN)
			errorstream << "  command name: TOCLIENT_DEATHSCREEN" << std::endl;
		else if (command == TOCLIENT_MEDIA)
			errorstream << "  command name: TOCLIENT_MEDIA" << std::endl;
		else if (command == TOCLIENT_TOOLDEF)
			errorstream << "  command name: TOCLIENT_TOOLDEF" << std::endl;
		else if (command == TOCLIENT_NODEDEF)
			errorstream << "  command name: TOCLIENT_NODEDEF" << std::endl;
		else if (command == TOCLIENT_CRAFTITEMDEF)
			errorstream << "  command name: TOCLIENT_CRAFTITEMDEF" << std::endl;
		else if (command == TOCLIENT_ANNOUNCE_MEDIA)
			errorstream << "  command name: TOCLIENT_ANNOUNCE_MEDIA" << std::endl;
		else if (command == TOCLIENT_ITEMDEF)
			errorstream << "  command name: TOCLIENT_ITEMDEF" << std::endl;
		else if (command == TOCLIENT_PLAY_SOUND)
			errorstream << "  command name: TOCLIENT_PLAY_SOUND" << std::endl;
		else if (command == TOCLIENT_STOP_SOUND)
			errorstream << "  command name: TOCLIENT_STOP_SOUND" << std::endl;
		else if (command == TOCLIENT_PRIVILEGES)
			errorstream << "  command name: TOCLIENT_PRIVILEGES" << std::endl;
		else if (command == TOCLIENT_INVENTORY_FORMSPEC)
			errorstream << "  command name: TOCLIENT_INVENTORY_FORMSPEC" << std::endl;
		else if (command == TOCLIENT_DETACHED_INVENTORY)
			errorstream << "  command name: TOCLIENT_DETACHED_INVENTORY" << std::endl;
		else if (command == TOCLIENT_SHOW_FORMSPEC)
			errorstream << "  command name: TOCLIENT_SHOW_FORMSPEC" << std::endl;
		else if (command == TOCLIENT_MOVEMENT)
			errorstream << "  command name: TOCLIENT_MOVEMENT" << std::endl;
		else if (command == TOCLIENT_SPAWN_PARTICLE)
			errorstream << "  command name: TOCLIENT_SPAWN_PARTICLE" << std::endl;
		else if (command == TOCLIENT_ADD_PARTICLESPAWNER)
			errorstream << "  command name: TOCLIENT_ADD_PARTICLESPAWNER" << std::endl;
		else if (command == TOCLIENT_DELETE_PARTICLESPAWNER_LEGACY)
			errorstream << "  command name: TOCLIENT_DELETE_PARTICLESPAWNER_LEGACY" << std::endl;
		else if (command == TOCLIENT_HUDADD)
			errorstream << "  command name: TOCLIENT_HUDADD" << std::endl;
		else if (command == TOCLIENT_HUDRM)
			errorstream << "  command name: TOCLIENT_HUDRM" << std::endl;
		else if (command == TOCLIENT_HUDCHANGE)
			errorstream << "  command name: TOCLIENT_HUDCHANGE" << std::endl;
		else if (command == TOCLIENT_HUD_SET_FLAGS)
			errorstream << "  command name: TOCLIENT_HUD_SET_FLAGS" << std::endl;
		else if (command == TOCLIENT_HUD_SET_PARAM)
			errorstream << "  command name: TOCLIENT_HUD_SET_PARAM" << std::endl;
		else if (command == TOCLIENT_BREATH)
			errorstream << "  command name: TOCLIENT_BREATH" << std::endl;
		else if (command == TOCLIENT_SET_SKY)
			errorstream << "  command name: TOCLIENT_SET_SKY" << std::endl;
		else if (command == TOCLIENT_OVERRIDE_DAY_NIGHT_RATIO)
			errorstream << "  command name: TOCLIENT_OVERRIDE_DAY_NIGHT_RATIO" << std::endl;
		else if (command == TOCLIENT_LOCAL_PLAYER_ANIMATIONS)
			errorstream << "  command name: TOCLIENT_LOCAL_PLAYER_ANIMATIONS" << std::endl;
		else if (command == TOCLIENT_EYE_OFFSET)
			errorstream << "  command name: TOCLIENT_EYE_OFFSET" << std::endl;
		else if (command == TOCLIENT_DELETE_PARTICLESPAWNER)
			errorstream << "  command name: TOCLIENT_DELETE_PARTICLESPAWNER" << std::endl;
		else if (command == TOCLIENT_CLOUD_PARAMS)
			errorstream << "  command name: TOCLIENT_CLOUD_PARAMS" << std::endl;
		else if (command == TOCLIENT_FADE_SOUND)
			errorstream << "  command name: TOCLIENT_FADE_SOUND" << std::endl;
		else if (command == TOCLIENT_UPDATE_PLAYER_LIST)
			errorstream << "  command name: TOCLIENT_UPDATE_PLAYER_LIST" << std::endl;
		else if (command == TOCLIENT_MODCHANNEL_MSG)
			errorstream << "  command name: TOCLIENT_MODCHANNEL_MSG" << std::endl;
		else if (command == TOCLIENT_MODCHANNEL_SIGNAL)
			errorstream << "  command name: TOCLIENT_MODCHANNEL_SIGNAL" << std::endl;
		else if (command == TOCLIENT_NODEMETA_CHANGED)
			errorstream << "  command name: TOCLIENT_NODEMETA_CHANGED" << std::endl;
		else if (command == TOCLIENT_SET_SUN)
			errorstream << "  command name: TOCLIENT_SET_SUN" << std::endl;
		else if (command == TOCLIENT_SET_MOON)
			errorstream << "  command name: TOCLIENT_SET_MOON" << std::endl;
		else if (command == TOCLIENT_SET_STARS)
			errorstream << "  command name: TOCLIENT_SET_STARS" << std::endl;
		else if (command == TOCLIENT_SRP_BYTES_S_B)
			errorstream << "  command name: TOCLIENT_SRP_BYTES_S_B" << std::endl;
		else if (command == TOCLIENT_FORMSPEC_PREPEND)
			errorstream << "  command name: TOCLIENT_FORMSPEC_PREPEND" << std::endl;
		else if (command == TOCLIENT_MINIMAP_MODES)
			errorstream << "  command name: TOCLIENT_MINIMAP_MODES" << std::endl;
		else if (command == TOCLIENT_NUM_MSG_TYPES)
			errorstream << "  command name: TOCLIENT_NUM_MSG_TYPES" << std::endl;
	}
}

bool NetworkPacket::encrypt(std::string key)
{
	std::string data((const char*)(m_data.data()), m_datasize);
	
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

	m_read_offset = 0;
	m_datasize = data_to_write.size();
	m_data.resize(m_datasize);
	memcpy(&m_data[0], data_to_write.c_str(), m_datasize);

	return true;
}
