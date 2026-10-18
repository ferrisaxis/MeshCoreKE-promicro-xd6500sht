#include "MyMesh.h"

#if defined(WITH_VIRTUAL_ROOMS)

#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint32_t ROOM_CONFIG_MAGIC = 0x524F4F4D; // ROOM
constexpr uint8_t ROOM_CONFIG_VERSION = 2;
constexpr uint32_t ROOM_ADVERT_SPACING_MS = 7000;
constexpr uint32_t ROOM_PUSH_DELAY_MS = 2000;
constexpr uint32_t ROOM_PUSH_INTERVAL_MS = 1200;
constexpr uint32_t ROOM_POST_SYNC_DELAY_S = 6;
constexpr uint32_t ROOM_ACK_TIMEOUT_FLOOD_MS = 12000;
constexpr uint32_t ROOM_ACK_TIMEOUT_BASE_MS = 4000;
constexpr uint32_t ROOM_ACK_TIMEOUT_HOP_MS = 2000;
constexpr uint32_t ROOM_RESPONSE_DELAY_MS = 300;
constexpr uint32_t ROOM_TEXT_ACK_DELAY_MS = 200;
constexpr uint8_t ROOM_REQ_STATUS = 0x01;
constexpr uint8_t ROOM_REQ_KEEP_ALIVE = 0x02;
constexpr uint8_t ROOM_REQ_TELEMETRY = 0x03;
constexpr uint8_t ROOM_REQ_ACCESS_LIST = 0x05;
constexpr char ROOM_CONFIG_FILE[] = "/rooms.cfg";

struct VirtualRoomsConfigV1 {
  uint32_t magic;
  uint8_t version;
  uint16_t advert_interval_minutes;
  uint8_t flood_advert_interval_hours;
  VirtualRoomConfig rooms[VIRTUAL_ROOM_COUNT];
};

bool isExact(const char* text, const char* value) { return strcmp(text, value) == 0; }

bool isSharedRoomGet(const char* key) {
  return isExact(key, "radio") || isExact(key, "freq") || isExact(key, "tx") ||
         isExact(key, "af") || isExact(key, "dutycycle") || isExact(key, "int.thresh") ||
         isExact(key, "cad") || isExact(key, "radio.rxgain") ||
         isExact(key, "radio.fem.rxgain") || isExact(key, "radio.fem.txgain") ||
         isExact(key, "rxdelay") ||
         isExact(key, "agc.reset.interval") || isExact(key, "path.hash.mode") ||
         isExact(key, "multi.acks") || isExact(key, "txdelay") ||
         isExact(key, "direct.txdelay") || isExact(key, "adc.multiplier") ||
         isExact(key, "lat") || isExact(key, "lon") || isExact(key, "owner.info") ||
         isExact(key, "flood.max") || isExact(key, "flood.max.unscoped") ||
         isExact(key, "flood.max.advert") || isExact(key, "loop.detect") ||
         isExact(key, "bootloader.ver") || isExact(key, "pwrmgt.support") ||
         isExact(key, "pwrmgt.source") || isExact(key, "pwrmgt.bootreason") ||
         isExact(key, "pwrmgt.bootmv") || isExact(key, "extra.sf") ||
         isExact(key, "bridge.type") || isExact(key, "bridge.enabled") ||
         isExact(key, "bridge.delay") || isExact(key, "bridge.source") ||
         isExact(key, "bridge.baud") || isExact(key, "bridge.channel");
}

bool parseRoomIndex(const char* command, const char* prefix, int& room_idx, const char*& suffix) {
  const size_t prefix_len = strlen(prefix);
  if (strncmp(command, prefix, prefix_len) != 0) return false;
  const char c = command[prefix_len];
  if (c < '1' || c > '5') return false;
  room_idx = c - '1';
  suffix = command + prefix_len + 1;
  return *suffix == 0 || *suffix == '.' || *suffix == ' ';
}

bool parseUnsigned(const char* text, unsigned long max_value, unsigned long& value) {
  if (!*text) return false;
  char* end;
  value = strtoul(text, &end, 10);
  return *end == 0 && end != text && value <= max_value;
}
}

int MyMesh::roomIndexFor(const mesh::LocalIdentity* id) const {
  if (!id) return -1;
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i)
    if (id == &rooms[i].id) return i;
  return -1;
}

const mesh::LocalIdentity* MyMesh::findLocalIdentity(const uint8_t* hash) const {
  if (self_id.isHashMatch(hash)) return &self_id;
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i)
    if (room_config.rooms[i].enabled && rooms[i].id.isHashMatch(hash)) return &rooms[i].id;
  return nullptr;
}

bool MyMesh::isLocalIdentity(const mesh::Identity& id) const {
  if (self_id.matches(id)) return true;
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i)
    if (rooms[i].id.matches(id)) return true;
  return false;
}

bool MyMesh::isTraceHop(const uint8_t* hash, uint8_t hash_len) const {
  if (self_id.isHashMatch(hash, hash_len)) return true;
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i)
    if (room_config.rooms[i].enabled && rooms[i].id.isHashMatch(hash, hash_len)) return true;
  return false;
}

bool MyMesh::saveVirtualRoomsConfig() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(ROOM_CONFIG_FILE);
  File file = _fs->open(ROOM_CONFIG_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(ROOM_CONFIG_FILE, "w");
#else
  File file = _fs->open(ROOM_CONFIG_FILE, "w", true);
#endif
  if (!file) return false;
  bool ok = file.write((const uint8_t*)&room_config, sizeof(room_config)) == sizeof(room_config);
  file.close();
  return ok;
}

void MyMesh::storeRoomPost(int room_idx, const mesh::Identity& author, const char* text) {
  VirtualRoomState& room = rooms[room_idx];
  VirtualRoomPost& post = room.posts[room.next_post_idx];
  post.author = author;
  StrHelper::strncpy(post.text, text, sizeof(post.text));
  post.timestamp = getRTCClock()->getCurrentTimeUnique();
  room.next_post_idx = (room.next_post_idx + 1) % ROOM_POST_COUNT;
  room.num_posted++;
  room.next_push = futureMillis(ROOM_PUSH_DELAY_MS);
}

uint8_t MyMesh::roomUnsyncedCount(int room_idx, ClientInfo* client) {
  uint8_t count = 0;
  for (const VirtualRoomPost& post : rooms[room_idx].posts)
    if (post.timestamp > client->extra.room.sync_since && !post.author.matches(client->id)) ++count;
  return count;
}

bool MyMesh::roomProcessAck(const uint8_t* ack) {
  for (int room_idx = 0; room_idx < VIRTUAL_ROOM_COUNT; ++room_idx) {
    ClientACL& contacts = rooms[room_idx].acl;
    for (int j = 0; j < contacts.getNumClients(); ++j) {
      ClientInfo* client = contacts.getClientByIdx(j);
      if (client->extra.room.pending_ack &&
          memcmp(ack, &client->extra.room.pending_ack, 4) == 0) {
        client->extra.room.pending_ack = 0;
        client->extra.room.push_failures = 0;
        client->extra.room.sync_since = client->extra.room.push_post_timestamp;
        return true;
      }
    }
  }
  return false;
}

void MyMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  if (roomProcessAck((const uint8_t*)&ack_crc)) packet->markDoNotRetransmit();
}

void MyMesh::roomPushPost(int room_idx, ClientInfo* client, VirtualRoomPost& post) {
  VirtualRoomState& room = rooms[room_idx];
  int len = 0;
  memcpy(&reply_data[len], &post.timestamp, 4); len += 4;
  uint8_t attempt;
  getRNG()->random(&attempt, 1);
  reply_data[len++] = (TXT_TYPE_SIGNED_PLAIN << 2) | (attempt & 3);
  memcpy(&reply_data[len], post.author.pub_key, 4); len += 4;
  size_t text_len = strlen(post.text);
  memcpy(&reply_data[len], post.text, text_len); len += text_len;
  mesh::Utils::sha256((uint8_t*)&client->extra.room.pending_ack, 4, reply_data, len,
                      client->id.pub_key, PUB_KEY_SIZE);
  client->extra.room.push_post_timestamp = post.timestamp;

  mesh::Packet* packet = createDatagram(room.id, PAYLOAD_TYPE_TXT_MSG, client->id,
                                        client->shared_secret, reply_data, len);
  if (!packet) {
    client->extra.room.pending_ack = 0;
    return;
  }
  if (client->out_path_len == OUT_PATH_UNKNOWN) {
    sendFloodScoped(default_scope, packet, 0, _prefs.path_hash_mode + 1);
    client->extra.room.ack_timeout = futureMillis(ROOM_ACK_TIMEOUT_FLOOD_MS);
  } else {
    sendDirect(packet, client->out_path, client->out_path_len);
    uint8_t hops = client->out_path_len & 63;
    client->extra.room.ack_timeout = futureMillis(ROOM_ACK_TIMEOUT_BASE_MS + ROOM_ACK_TIMEOUT_HOP_MS * (hops + 1));
  }
  room.num_pushes++;
}

void MyMesh::roomAnonRecv(int room_idx, mesh::Packet* packet, const uint8_t* secret,
                          const mesh::Identity& sender, uint8_t* data, size_t len) {
  if (packet->getPayloadType() != PAYLOAD_TYPE_ANON_REQ || len < 9 || len >= MAX_PACKET_PAYLOAD) return;
  VirtualRoomState& room = rooms[room_idx];
  VirtualRoomConfig& cfg = room_config.rooms[room_idx];
  uint32_t timestamp, sync_since;
  memcpy(&timestamp, data, 4);
  memcpy(&sync_since, &data[4], 4);
  data[len] = 0;
  ClientInfo* client = nullptr;
  if (data[8] == 0) client = room.acl.getClient(sender.pub_key, PUB_KEY_SIZE);
  if (!client) {
    uint8_t permissions;
    if (cfg.admin_password[0] && strcmp((char*)&data[8], cfg.admin_password) == 0)
      permissions = PERM_ACL_ADMIN;
    else if (cfg.guest_password[0] && strcmp((char*)&data[8], cfg.guest_password) == 0)
      permissions = PERM_ACL_READ_WRITE;
    else if (cfg.allow_read_only)
      permissions = PERM_ACL_GUEST;
    else return;

    client = room.acl.putClient(sender, 0);
    if (timestamp <= client->last_timestamp) return;
    client->permissions = permissions;
    client->extra.room.sync_since = sync_since;
    client->extra.room.pending_ack = 0;
    client->extra.room.push_failures = 0;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);
    room.dirty_contacts_expiry = futureMillis(5000);
  }
  client->last_timestamp = timestamp;
  client->last_activity = getRTCClock()->getCurrentTime();
  if (packet->isRouteFlood()) client->out_path_len = OUT_PATH_UNKNOWN;

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);
  reply_data[4] = 0; // RESP_SERVER_LOGIN_OK
  reply_data[5] = 0;
  reply_data[6] = client->isAdmin() ? 1 : (client->permissions == 0 ? 2 : 0);
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);
  reply_data[12] = 1; // protocol version level
  room.next_push = futureMillis(ROOM_PUSH_DELAY_MS);

  mesh::Packet* reply;
  if (packet->isRouteFlood()) {
    reply = createPathReturn(room.id, sender, client->shared_secret, packet->path,
                             packet->path_len, PAYLOAD_TYPE_RESPONSE, reply_data, 13);
    if (reply) sendFloodReply(reply, ROOM_RESPONSE_DELAY_MS, packet->getPathHashSize());
  } else {
    reply = createDatagram(room.id, PAYLOAD_TYPE_RESPONSE, sender, client->shared_secret, reply_data, 13);
    if (reply) {
      if (client->out_path_len != OUT_PATH_UNKNOWN)
        sendDirect(reply, client->out_path, client->out_path_len, ROOM_RESPONSE_DELAY_MS);
      else sendFloodReply(reply, ROOM_RESPONSE_DELAY_MS, packet->getPathHashSize());
    }
  }
}

struct VirtualRoomServerStats {
  uint16_t batt_milli_volts, curr_tx_queue_len;
  int16_t noise_floor, last_rssi;
  uint32_t n_packets_recv, n_packets_sent, total_air_time_secs, total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct, n_recv_flood, n_recv_direct;
  uint16_t err_events;
  int16_t last_snr;
  uint16_t n_direct_dups, n_flood_dups, n_posted, n_post_push;
};

int MyMesh::roomRequest(int room_idx, ClientInfo* client, uint32_t tag,
                        const uint8_t* request, size_t len) {
  if (!len) return 0;
  memcpy(reply_data, &tag, 4);
  if (request[0] == ROOM_REQ_STATUS) {
    VirtualRoomServerStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables*)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables*)getTables())->getNumFloodDups();
    stats.n_posted = rooms[room_idx].num_posted;
    stats.n_post_push = rooms[room_idx].num_pushes;
    memcpy(&reply_data[4], &stats, sizeof(stats));
    return 4 + sizeof(stats);
  }
  if (request[0] == ROOM_REQ_TELEMETRY) {
    if (len < 2) return 0;
    uint8_t mask = (client->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST ? 0 : ~request[1];
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    sensors.querySensors(mask, telemetry);
    float temperature = board.getMCUTemperature();
    if (!isnan(temperature)) telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature);
    uint8_t size = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), size);
    return 4 + size;
  }
  if (request[0] == ROOM_REQ_ACCESS_LIST && client->isAdmin() && len >= 3 &&
      request[1] == 0 && request[2] == 0) {
    int offset = 4;
    ClientACL& contacts = rooms[room_idx].acl;
    for (int i = 0; i < contacts.getNumClients() && offset + 7 <= sizeof(reply_data) - 4; ++i) {
      ClientInfo* member = contacts.getClientByIdx(i);
      if (!member->isAdmin()) continue;
      memcpy(&reply_data[offset], member->id.pub_key, 6); offset += 6;
      reply_data[offset++] = member->permissions;
    }
    return offset;
  }
  return 0;
}

void MyMesh::roomAdminCommand(int room_idx, const char* command, char* reply) {
  char prefix[4] = "";
  if (strlen(command) > 4 && command[2] == '|') {
    memcpy(prefix, command, 3);
    command += 3;
  }

  char response[160] = "";
  char routed[160];
  int n = -1;
  if (strncmp(command, "room.post", 9) == 0 &&
      (command[9] == 0 || command[9] == ' ')) {
    const char* text = command + 9;
    while (*text == ' ') ++text;
    if (!*text) strcpy(response, "ERR empty message");
    else {
      storeRoomPost(room_idx, rooms[room_idx].id, text);
      strcpy(response, "OK");
    }
  } else if (strcmp(command, "get role") == 0) {
    strcpy(response, "> room_server");
  } else if (strcmp(command, "get repeat") == 0) {
    snprintf(response, sizeof(response), "> %s", _prefs.disable_fwd ? "off" : "on");
  } else if (strcmp(command, "ver") == 0 || strcmp(command, "board") == 0 ||
             strcmp(command, "clock") == 0 || strcmp(command, "power") == 0) {
    // Shared device information is safe to read from either service identity.
    strcpy(routed, command);
    _cli.handleCommand(1, routed, response);
  } else if (strcmp(command, "advert") == 0 || strcmp(command, "advert.zerohop") == 0) {
    n = snprintf(routed, sizeof(routed), "room.%d.%s", room_idx + 1, command);
  } else if (strncmp(command, "setperm ", 8) == 0) {
    n = snprintf(routed, sizeof(routed), "room.%d.setperm %s", room_idx + 1, command + 8);
  } else if (strncmp(command, "password ", 9) == 0) {
    n = snprintf(routed, sizeof(routed), "set room.%d.admin.password %s", room_idx + 1, command + 9);
  } else if (strcmp(command, "get acl") == 0 || strncmp(command, "get acl ", 8) == 0) {
    n = snprintf(routed, sizeof(routed), "get room.%d.acl%s", room_idx + 1, command + 7);
  } else if (strcmp(command, "get stats") == 0) {
    n = snprintf(routed, sizeof(routed), "get room.%d.stats", room_idx + 1);
  } else if (strncmp(command, "get ", 4) == 0 || strncmp(command, "set ", 4) == 0) {
    const bool setting = command[0] == 's';
    const char* key = command + 4;
    if (!setting && (isExact(key, "advert.interval") ||
                     isExact(key, "flood.advert.interval"))) {
      strcpy(routed, command);
      _cli.handleCommand(1, routed, response);
    } else if (strcmp(key, "name") == 0 || strncmp(key, "name ", 5) == 0 ||
               strcmp(key, "guest.password") == 0 || strncmp(key, "guest.password ", 15) == 0 ||
               strcmp(key, "allow.read.only") == 0 || strncmp(key, "allow.read.only ", 16) == 0 ||
               strcmp(key, "public.key") == 0) {
      n = snprintf(routed, sizeof(routed), "%s room.%d.%s", setting ? "set" : "get", room_idx + 1, key);
    } else if (strncmp(key, "prv.key", 7) == 0) {
      strcpy(response, "Private key restore: use repeater admin CLI");
    } else if (!setting && isSharedRoomGet(key)) {
      // These values describe the one physical repeater shared by all rooms.
      strcpy(routed, command);
      _cli.handleCommand(1, routed, response);
    } else {
      strcpy(response, "Use repeater admin CLI for shared settings");
    }
  } else {
    strcpy(response, "Unknown command");
  }

  if (n >= 0) {
    if ((size_t)n >= sizeof(routed) || !handleVirtualRoomCommand(routed, response))
      strcpy(response, "Err - command too long or unknown");
  }
  snprintf(reply, 161, "%s%s", prefix, response);
}

void MyMesh::roomPeerRecv(int room_idx, mesh::Packet* packet, uint8_t type, int sender_idx,
                          const uint8_t* secret, uint8_t* data, size_t len) {
  VirtualRoomState& room = rooms[room_idx];
  int idx = matching_peer_indexes[sender_idx];
  if (idx < 0 || idx >= room.acl.getNumClients()) return;
  ClientInfo* client = room.acl.getClientByIdx(idx);

  if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && len < MAX_PACKET_PAYLOAD) {
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);
    uint8_t flags = data[4] >> 2;
    if (flags != TXT_TYPE_PLAIN && flags != TXT_TYPE_CLI_DATA && flags != TXT_TYPE_CLI_COMMAND) return;
    if (timestamp < client->last_timestamp) return;
    bool retry = timestamp == client->last_timestamp;
    client->last_timestamp = timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->extra.room.push_failures = 0;
    data[len] = 0;

    uint8_t temp[166] = {};
    bool send_ack = false;
    if (flags == TXT_TYPE_PLAIN) {
      if ((client->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) return;
      if (!retry) storeRoomPost(room_idx, client->id, (const char*)&data[5]);
      send_ack = true;
    } else if (client->isAdmin() && !retry) {
      roomAdminCommand(room_idx, (const char*)&data[5], (char*)&temp[5]);
      temp[4] = TXT_TYPE_CLI_DATA << 2;
    }

    uint32_t delay = 0;
    if (send_ack) {
      uint32_t ack_hash;
      mesh::Utils::sha256((uint8_t*)&ack_hash, 4, data, 5 + strlen((const char*)&data[5]),
                          client->id.pub_key, PUB_KEY_SIZE);
      mesh::Packet* ack = createAck(ack_hash);
      if (ack) {
        if (client->out_path_len == OUT_PATH_UNKNOWN)
          sendFloodReply(ack, ROOM_TEXT_ACK_DELAY_MS, packet->getPathHashSize());
        else sendDirect(ack, client->out_path, client->out_path_len, ROOM_TEXT_ACK_DELAY_MS);
      }
      delay = ROOM_TEXT_ACK_DELAY_MS + ROOM_RESPONSE_DELAY_MS;
    }
    size_t text_len = strlen((char*)&temp[5]);
    if (text_len) {
      uint32_t now = getRTCClock()->getCurrentTimeUnique();
      if (now == timestamp) ++now;
      memcpy(temp, &now, 4);
      mesh::Packet* response = createDatagram(room.id, PAYLOAD_TYPE_TXT_MSG, client->id, secret,
                                               temp, 5 + text_len);
      if (response) {
        if (client->out_path_len == OUT_PATH_UNKNOWN)
          sendFloodReply(response, delay, packet->getPathHashSize());
        else sendDirect(response, client->out_path, client->out_path_len, delay);
      }
    }
    return;
  }
  if (type != PAYLOAD_TYPE_REQ || len < 5) return;
  uint32_t timestamp;
  memcpy(&timestamp, data, 4);
  if (timestamp < client->last_timestamp) return;
  client->last_timestamp = timestamp;
  client->last_activity = getRTCClock()->getCurrentTime();
  client->extra.room.push_failures = 0;

  if (data[4] == ROOM_REQ_KEEP_ALIVE && packet->isRouteDirect()) {
    uint32_t since = 0;
    if (len >= 9) memcpy(&since, &data[5], 4);
    if (since) client->extra.room.sync_since = since;
    client->extra.room.pending_ack = 0;
    if (client->out_path_len != OUT_PATH_UNKNOWN) {
      uint8_t keep_alive[9];
      memcpy(keep_alive, data, 4);
      keep_alive[4] = data[4];
      memcpy(&keep_alive[5], &since, 4);
      uint32_t ack_hash;
      mesh::Utils::sha256((uint8_t*)&ack_hash, 4, keep_alive, 9, client->id.pub_key, PUB_KEY_SIZE);
      mesh::Packet* response = createAck(ack_hash);
      if (response) {
        response->payload[response->payload_len++] = roomUnsyncedCount(room_idx, client);
        sendDirect(response, client->out_path, client->out_path_len, ROOM_RESPONSE_DELAY_MS);
      }
    }
    return;
  }
  int reply_len = roomRequest(room_idx, client, timestamp, &data[4], len - 4);
  if (reply_len <= 0) return;
  mesh::Packet* response;
  if (packet->isRouteFlood()) {
    response = createPathReturn(room.id, client->id, secret, packet->path, packet->path_len,
                                PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
    if (response) sendFloodReply(response, ROOM_RESPONSE_DELAY_MS, packet->getPathHashSize());
  } else {
    response = createDatagram(room.id, PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
    if (response) {
      if (client->out_path_len == OUT_PATH_UNKNOWN)
        sendFloodReply(response, ROOM_RESPONSE_DELAY_MS, packet->getPathHashSize());
      else sendDirect(response, client->out_path, client->out_path_len, ROOM_RESPONSE_DELAY_MS);
    }
  }
}

bool MyMesh::roomPathRecv(int room_idx, mesh::Packet* packet, int sender_idx,
                          const uint8_t* secret, uint8_t* path, uint8_t path_len,
                          uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  (void)packet; (void)secret;
  VirtualRoomState& room = rooms[room_idx];
  int idx = matching_peer_indexes[sender_idx];
  if (idx >= 0 && idx < room.acl.getNumClients()) {
    ClientInfo* client = room.acl.getClientByIdx(idx);
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  }
  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) roomProcessAck(extra);
  return false;
}

void MyMesh::startRoomAdverts(uint32_t delay_ms, bool flood) {
  next_room_advert_idx = 0;
  while (next_room_advert_idx < VIRTUAL_ROOM_COUNT &&
         !room_config.rooms[next_room_advert_idx].enabled) ++next_room_advert_idx;
  room_advert_flood = flood;
  next_room_advert = next_room_advert_idx < VIRTUAL_ROOM_COUNT
    ? futureMillis(delay_ms + ROOM_ADVERT_SPACING_MS) : 0;
}

void MyMesh::loopVirtualRooms() {
  if (next_room_advert && millisHasNowPassed(next_room_advert)) {
    while (next_room_advert_idx < VIRTUAL_ROOM_COUNT &&
           !room_config.rooms[next_room_advert_idx].enabled) ++next_room_advert_idx;
    if (next_room_advert_idx < VIRTUAL_ROOM_COUNT)
      sendRoomAdvert(next_room_advert_idx++, room_advert_flood);
    while (next_room_advert_idx < VIRTUAL_ROOM_COUNT &&
           !room_config.rooms[next_room_advert_idx].enabled) ++next_room_advert_idx;
    next_room_advert = next_room_advert_idx < VIRTUAL_ROOM_COUNT
      ? futureMillis(ROOM_ADVERT_SPACING_MS) : 0;
  }

  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i) {
    VirtualRoomState& room = rooms[i];
    if (!room_config.rooms[i].enabled) continue;
    if (room.dirty_contacts_expiry && millisHasNowPassed(room.dirty_contacts_expiry)) {
      room.acl.save(_fs);
      room.dirty_contacts_expiry = 0;
    }
  }

  // Only one room attempts a post push per loop, so one active room cannot
  // monopolise the shared outbound radio queue.
  int i = next_room_push_idx;
  next_room_push_idx = (next_room_push_idx + 1) % VIRTUAL_ROOM_COUNT;
  VirtualRoomState& room = rooms[i];
  if (!room_config.rooms[i].enabled || !room.acl.getNumClients() ||
      (room.next_push && !millisHasNowPassed(room.next_push))) return;
  for (int j = 0; j < room.acl.getNumClients(); ++j) {
    ClientInfo* client = room.acl.getClientByIdx(j);
    if (client->extra.room.pending_ack && millisHasNowPassed(client->extra.room.ack_timeout)) {
      client->extra.room.pending_ack = 0;
      client->extra.room.push_failures++;
    }
  }
  ClientInfo* client = room.acl.getClientByIdx(room.next_client_idx);
  bool pushed = false;
  if (!client->extra.room.pending_ack && client->last_activity && client->extra.room.push_failures < 3) {
    uint32_t now = getRTCClock()->getCurrentTime();
    for (int k = 0, post_idx = room.next_post_idx; k < ROOM_POST_COUNT; ++k) {
      VirtualRoomPost& post = room.posts[post_idx];
      if (post.timestamp && now >= post.timestamp + ROOM_POST_SYNC_DELAY_S &&
          post.timestamp > client->extra.room.sync_since && !post.author.matches(client->id)) {
        roomPushPost(i, client, post);
        pushed = true;
        break;
      }
      post_idx = (post_idx + 1) % ROOM_POST_COUNT;
    }
  }
  room.next_client_idx = (room.next_client_idx + 1) % room.acl.getNumClients();
  room.next_push = futureMillis(pushed ? ROOM_PUSH_INTERVAL_MS : ROOM_PUSH_INTERVAL_MS / 8);
}

void MyMesh::clearRoomRuntime(int room_idx) {
  VirtualRoomState& room = rooms[room_idx];
  memset(room.posts, 0, sizeof(room.posts));
  room.num_posted = room.num_pushes = 0;
  room.next_post_idx = room.next_client_idx = 0;
  room.next_push = 0;
  room.dirty_contacts_expiry = 0;
}

void MyMesh::loadVirtualRooms() {
  memset(&room_config, 0, sizeof(room_config));
  room_config.magic = ROOM_CONFIG_MAGIC;
  room_config.version = ROOM_CONFIG_VERSION;
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i)
    snprintf(room_config.rooms[i].name, sizeof(room_config.rooms[i].name), "room%d", i + 1);

  bool first_boot = !_fs->exists(ROOM_CONFIG_FILE);
  bool migrated = false;
  if (!first_boot) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(ROOM_CONFIG_FILE, "r");
#else
    File file = _fs->open(ROOM_CONFIG_FILE);
#endif
    bool valid = false;
    if (file) {
      if (file.size() == sizeof(VirtualRoomsConfig)) {
        VirtualRoomsConfig stored;
        valid = file.read((uint8_t*)&stored, sizeof(stored)) == sizeof(stored) &&
                stored.magic == ROOM_CONFIG_MAGIC && stored.version == ROOM_CONFIG_VERSION;
        if (valid) room_config = stored;
      } else if (file.size() == sizeof(VirtualRoomsConfigV1)) {
        VirtualRoomsConfigV1 stored;
        valid = file.read((uint8_t*)&stored, sizeof(stored)) == sizeof(stored) &&
                stored.magic == ROOM_CONFIG_MAGIC && stored.version == 1;
        if (valid) {
          memcpy(room_config.rooms, stored.rooms, sizeof(room_config.rooms));
          migrated = true;
        }
      }
    }
    if (file) file.close();
    if (valid) {
      for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i) {
        room_config.rooms[i].name[sizeof(room_config.rooms[i].name) - 1] = 0;
        room_config.rooms[i].admin_password[sizeof(room_config.rooms[i].admin_password) - 1] = 0;
        room_config.rooms[i].guest_password[sizeof(room_config.rooms[i].guest_password) - 1] = 0;
      }
    } else {
      // Invalid settings must never accidentally enable a room.
      MESH_DEBUG_PRINTLN("Virtual rooms: invalid settings; all rooms remain off");
    }
  }

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#else
  IdentityStore store(*_fs, "/identity");
#endif
  for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i) {
    VirtualRoomState& room = rooms[i];
    clearRoomRuntime(i);
    char id_name[12];
    char contacts_name[24];
    snprintf(id_name, sizeof(id_name), "_room%d", i + 1);
    snprintf(contacts_name, sizeof(contacts_name), "/room%d_contacts", i + 1);
    room.acl.setFilename(contacts_name);
    if (!store.load(id_name, room.id)) {
      mesh::LocalIdentity generated;
      bool unique = false;
      for (int attempt = 0; attempt < 32 && !unique; ++attempt) {
        generated = mesh::LocalIdentity(getRNG());
        uint8_t hash = generated.pub_key[0];
        unique = hash != 0 && hash != 0xFF && hash != self_id.pub_key[0];
        for (int j = 0; j < i; ++j) unique = unique && hash != rooms[j].id.pub_key[0];
      }
      if (unique && store.save(id_name, generated)) {
        room.id = generated;
      } else {
        room_config.rooms[i].enabled = 0;
        MESH_DEBUG_PRINTLN("Virtual room %d: identity generation/save failed", i + 1);
      }
    }
    // One-byte destination hashes must not collide with the repeater or another room.
    for (int j = -1; j < i; ++j) {
      uint8_t used_hash = j < 0 ? self_id.pub_key[0] : rooms[j].id.pub_key[0];
      if (room.id.pub_key[0] == used_hash) room_config.rooms[i].enabled = 0;
    }
    room.acl.load(_fs, room.id);
  }
  if (first_boot || migrated) saveVirtualRoomsConfig();
}

void MyMesh::sendRoomAdvert(int room_idx, bool flood, uint32_t delay_ms) {
  if (!room_config.rooms[room_idx].enabled) return;
  const char* name = room_config.rooms[room_idx].name;
  uint8_t data[MAX_ADVERT_DATA_SIZE];
  uint8_t size;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name);
    size = builder.encodeTo(data);
  } else if (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name, sensors.node_lat, sensors.node_lon);
    size = builder.encodeTo(data);
  } else {
    AdvertDataBuilder builder(ADV_TYPE_ROOM, name, _prefs.node_lat, _prefs.node_lon);
    size = builder.encodeTo(data);
  }
  mesh::Packet* packet = createAdvert(rooms[room_idx].id, data, size);
  if (!packet) return;
  if (flood) sendFloodScoped(default_scope, packet, delay_ms, _prefs.path_hash_mode + 1);
  else sendZeroHop(packet, delay_ms);
}

bool MyMesh::handleVirtualRoomCommand(const char* command, char* reply) {
  if (isExact(command, "get rooms")) {
    size_t used = 0;
    for (int i = 0; i < VIRTUAL_ROOM_COUNT; ++i) {
      int n = snprintf(reply + used, 160 - used, "%s%d:%s:%.16s", i ? " " : "", i + 1,
                       room_config.rooms[i].enabled ? "on" : "off", room_config.rooms[i].name);
      if (n < 0 || (size_t)n >= 160 - used) break;
      used += n;
    }
    return true;
  }
  const char* suffix;
  int i;
  bool is_get = parseRoomIndex(command, "get room.", i, suffix);
  bool is_set = !is_get && parseRoomIndex(command, "set room.", i, suffix);
  bool is_action = !is_get && !is_set && parseRoomIndex(command, "room.", i, suffix);
  if (!is_get && !is_set && !is_action) return false;
  VirtualRoomConfig& cfg = room_config.rooms[i];
  VirtualRoomState& room = rooms[i];

  if (is_get) {
    if (isExact(suffix, "")) {
      snprintf(reply, 160, "%d:%s name=%s clients=%d posts=%u admin=%s guest=%s readonly=%s", i + 1,
               cfg.enabled ? "on" : "off", cfg.name, room.acl.getNumClients(), room.num_posted,
               cfg.admin_password[0] ? "set" : "unset", cfg.guest_password[0] ? "set" : "unset",
               cfg.allow_read_only ? "on" : "off");
    } else if (isExact(suffix, ".name")) {
      snprintf(reply, 160, "> %s", cfg.name);
    } else if (isExact(suffix, ".guest.password")) {
      snprintf(reply, 160, "> %s", cfg.guest_password);
    } else if (isExact(suffix, ".public.key")) {
      strcpy(reply, "> ");
      mesh::Utils::toHex(reply + 2, room.id.pub_key, PUB_KEY_SIZE);
    } else if (isExact(suffix, ".allow.read.only")) {
      snprintf(reply, 160, "> %s", cfg.allow_read_only ? "on" : "off");
    } else if (isExact(suffix, ".stats")) {
      snprintf(reply, 160, "clients=%d posts=%u pushes=%u", room.acl.getNumClients(),
               room.num_posted, room.num_pushes);
    } else if (strncmp(suffix, ".acl", 4) == 0 && (suffix[4] == 0 || suffix[4] == ' ')) {
      unsigned long offset = 0;
      if (suffix[4] && !parseUnsigned(suffix + 5, MAX_CLIENTS, offset)) {
        strcpy(reply, "Err - invalid offset");
        return true;
      }
      size_t used = snprintf(reply, 160, "ACL %lu/%d:", offset, room.acl.getNumClients());
      for (int j = offset; j < room.acl.getNumClients() && j < (int)offset + 2; ++j) {
        ClientInfo* client = room.acl.getClientByIdx(j);
        if (used + 69 >= 160) break;
        used += snprintf(reply + used, 160 - used, " %d:", client->permissions & PERM_ACL_ROLE_MASK);
        mesh::Utils::toHex(reply + used, client->id.pub_key, PUB_KEY_SIZE);
        used += 64;
      }
    } else strcpy(reply, "Err - unknown room setting");
    return true;
  }

  if (is_set) {
    if (isExact(suffix, " on") || isExact(suffix, " off")) {
      bool on = suffix[1] == 'o' && suffix[2] == 'n';
      if (on && !cfg.admin_password[0]) strcpy(reply, "Err - set admin password first");
      else if (on && !room.id.pub_key[0]) strcpy(reply, "Err - room has no key");
      else {
        cfg.enabled = on;
        if (saveVirtualRoomsConfig()) {
          if (on) sendRoomAdvert(i, false, 1500);
          strcpy(reply, "OK");
        } else strcpy(reply, "Err - settings not saved");
      }
      return true;
    }
    if (strncmp(suffix, ".name ", 6) == 0) {
      const char* value = suffix + 6;
      if (strlen(value) >= sizeof(cfg.name) || !AdvertDataParser::isValidName(value))
        strcpy(reply, "Err - invalid name");
      else {
        strcpy(cfg.name, value);
        strcpy(reply, saveVirtualRoomsConfig() ? "OK" : "Err - settings not saved");
      }
      return true;
    }
    const char* password = nullptr;
    char* destination = nullptr;
    if (strncmp(suffix, ".admin.password ", 16) == 0) {
      password = suffix + 16;
      destination = cfg.admin_password;
    } else if (strncmp(suffix, ".guest.password ", 16) == 0) {
      password = suffix + 16;
      destination = cfg.guest_password;
    }
    if (password) {
      if (!*password || strlen(password) >= 16) strcpy(reply, "Err - password must be 1..15 chars");
      else {
        strcpy(destination, password);
        strcpy(reply, saveVirtualRoomsConfig() ? "OK" : "Err - settings not saved");
      }
      return true;
    }
    if (isExact(suffix, ".allow.read.only on") || isExact(suffix, ".allow.read.only off")) {
      cfg.allow_read_only = isExact(suffix, ".allow.read.only on");
      strcpy(reply, saveVirtualRoomsConfig() ? "OK" : "Err - settings not saved");
      return true;
    }
    if (strncmp(suffix, ".prv.key ", 9) == 0) {
      uint8_t key[PRV_KEY_SIZE];
      const char* value = suffix + 9;
      if (!mesh::Utils::fromHex(key, PRV_KEY_SIZE, value) || !mesh::LocalIdentity::validatePrivateKey(key)) {
        strcpy(reply, "Err - invalid 128-hex private key");
        return true;
      }
      mesh::LocalIdentity new_id;
      new_id.readFrom(key, PRV_KEY_SIZE);
      for (int j = -1; j < VIRTUAL_ROOM_COUNT; ++j) {
        if (j == i) continue;
        uint8_t hash = j < 0 ? self_id.pub_key[0] : rooms[j].id.pub_key[0];
        if (new_id.pub_key[0] == hash) {
          strcpy(reply, "Err - destination hash collision");
          return true;
        }
      }
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
      IdentityStore store(*_fs, "");
#else
      IdentityStore store(*_fs, "/identity");
#endif
      char id_name[12];
      snprintf(id_name, sizeof(id_name), "_room%d", i + 1);
      if (!store.save(id_name, new_id)) strcpy(reply, "Err - key not saved");
      else {
        room.id = new_id;
        room.acl.clear();
        clearRoomRuntime(i);
        if (cfg.enabled) sendRoomAdvert(i, false, 1500);
        strcpy(reply, "OK pubkey=");
        mesh::Utils::toHex(reply + 10, room.id.pub_key, PUB_KEY_SIZE);
      }
      return true;
    }
    strcpy(reply, "Err - unknown room setting");
    return true;
  }

  if (isExact(suffix, ".key.generate")) {
    mesh::LocalIdentity generated;
    bool unique = false;
    for (int attempt = 0; attempt < 32 && !unique; ++attempt) {
      generated = mesh::LocalIdentity(getRNG());
      uint8_t hash = generated.pub_key[0];
      unique = hash != 0 && hash != 0xFF && hash != self_id.pub_key[0];
      for (int j = 0; j < VIRTUAL_ROOM_COUNT; ++j)
        if (j != i) unique = unique && hash != rooms[j].id.pub_key[0];
    }
    if (!unique) strcpy(reply, "Err - no unique room hash");
    else {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
      IdentityStore store(*_fs, "");
#else
      IdentityStore store(*_fs, "/identity");
#endif
      char id_name[12];
      snprintf(id_name, sizeof(id_name), "_room%d", i + 1);
      if (!store.save(id_name, generated)) strcpy(reply, "Err - key not saved");
      else {
        room.id = generated;
        room.acl.clear();
        clearRoomRuntime(i);
        if (cfg.enabled) sendRoomAdvert(i, false, 1500);
        strcpy(reply, "OK pubkey=");
        mesh::Utils::toHex(reply + 10, room.id.pub_key, PUB_KEY_SIZE);
      }
    }
    return true;
  }
  if (strncmp(suffix, ".setperm ", 9) == 0) {
    const char* key = suffix + 9;
    if (strlen(key) < 66 || key[64] != ' ' || key[66] != 0 || key[65] < '0' || key[65] > '3') {
      strcpy(reply, "Err - use 64-hex public key and role 0..3");
      return true;
    }
    char hex[65];
    memcpy(hex, key, 64); hex[64] = 0;
    uint8_t pub[PUB_KEY_SIZE];
    if (!mesh::Utils::fromHex(pub, PUB_KEY_SIZE, hex) ||
        !room.acl.applyPermissions(room.id, pub, PUB_KEY_SIZE, key[65] - '0')) {
      strcpy(reply, "Err - invalid public key or role");
    } else {
      room.acl.save(_fs);
      strcpy(reply, "OK");
    }
    return true;
  }
  if (isExact(suffix, ".advert") || isExact(suffix, ".advert.zerohop")) {
    if (!cfg.enabled) strcpy(reply, "Err - room is off");
    else {
      sendRoomAdvert(i, isExact(suffix, ".advert"), 1500);
      strcpy(reply, "OK - advert queued");
    }
    return true;
  }
  if (strncmp(suffix, ".post ", 6) == 0) {
    if (!cfg.enabled || !suffix[6]) strcpy(reply, "Err - room is off or post empty");
    else {
      storeRoomPost(i, room.id, suffix + 6);
      strcpy(reply, "OK");
    }
    return true;
  }
  strcpy(reply, "Err - unknown room command");
  return true;
}

#endif
