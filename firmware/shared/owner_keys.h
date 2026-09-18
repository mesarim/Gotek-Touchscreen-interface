#pragma once
#include "control_auth.h"
#include <esp_random.h>
#ifndef GOTEK_AUTH_HOST_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

namespace GotekAuth {
struct Session {
  bool used=false;
  uint8_t mac[6]={},key[16]={},nonce[8]={};
  uint32_t sequence=0;
};
static Session sessions[64];
#ifndef GOTEK_AUTH_HOST_TEST
static portMUX_TYPE sessionMutex=portMUX_INITIALIZER_UNLOCKED;
struct SessionGuard {
  SessionGuard(){portENTER_CRITICAL(&sessionMutex);}
  ~SessionGuard(){portEXIT_CRITICAL(&sessionMutex);}
};
#else
struct SessionGuard { ~SessionGuard(){} };
#endif
// Caller holds SessionGuard; filesystem and HMAC work stays outside the lock.
inline Session* session(const uint8_t* mac) {
  Session* empty=nullptr;
  for(auto& s:sessions){
    if(s.used && !memcmp(s.mac,mac,6))return &s;
    if(!s.used && !empty)empty=&s;
  }
  return empty;
}
inline String keyPath(const uint8_t* mac) {
  char path[32];
  snprintf(path,sizeof(path),"/.gti-key-%02x%02x%02x%02x%02x%02x",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(path);
}
template<class FS> bool loadKey(FS& fs,const uint8_t* mac,uint8_t* key) {
  auto f=fs.open(keyPath(mac),"r");
  bool ok=f && f.size()==16 && f.read(key,16)==16;
  f.close();return ok;
}
template<class FS> bool storeKey(FS& fs,const uint8_t* mac,const uint8_t* key) {
  String path=keyPath(mac),tmp=path+".tmp";
  if(fs.exists(path))return false;
  if(fs.exists(tmp) && !fs.remove(tmp))return false;
  auto f=fs.open(tmp,"w");if(!f)return false;
  bool ok=f.write(key,16)==16;f.flush();ok=ok && f.size()==16;f.close();
  return ok && fs.rename(tmp,path);
}
// Key provisioning is allowed only during the local enrollment window.
// Known owners subsequently receive a signed challenge without the key.
template<class FS> bool pairReply(FS& fs,const uint8_t* owner,uint8_t* reply,
                                 bool enrollmentOpen) {
  uint8_t key[16];
  if(!loadKey(fs,owner,key)){
    if(!enrollmentOpen)return false;
    esp_fill_random(key,sizeof(key));
    if(!storeKey(fs,owner,key))return false;
  }
  uint8_t nonce[8];esp_fill_random(nonce,8);
  {
    SessionGuard guard;Session* s=session(owner);if(!s)return false;
    s->used=true;memcpy(s->mac,owner,6);memcpy(s->key,key,16);
    memcpy(s->nonce,nonce,8);s->sequence=0;
  }
  reply[25]=1; // PktHello.pad[2]: authenticated-control version
  reply[26]=enrollmentOpen?1:0;
  if(enrollmentOpen)memcpy(reply+27,key,16);
  return seal(reply,packetSize,key,nonce,0);
}
template<class FS> bool acceptReply(FS& fs,const uint8_t* mac,const uint8_t* reply,size_t n) {
  if(n!=packetSize || reply[25]!=1)return false;
  uint8_t key[16];bool known=loadKey(fs,mac,key);
  if(!known){if(reply[26]!=1)return false;memcpy(key,reply+27,16);}
  if(!authentic(reply,n,key))return false;
  if(!known && !storeKey(fs,mac,key))return false;
  SessionGuard guard;Session* s=session(mac);if(!s)return false;
  // Repeated copies of the same reply must not rewind the sequence counter.
  if(!s->used || memcmp(s->nonce,reply+nonceOffset,8))s->sequence=0;
  s->used=true;memcpy(s->mac,mac,6);memcpy(s->key,key,16);
  memcpy(s->nonce,reply+nonceOffset,8);return true;
}
inline bool signCommand(const uint8_t* mac,uint8_t* packet,size_t n) {
  Session copy;
  {
    SessionGuard guard;Session* s=session(mac);
    if(!s || !s->used || s->sequence==UINT32_MAX)return false;
    ++s->sequence;copy=*s;
  }
  return seal(packet,n,copy.key,copy.nonce,copy.sequence);
}
inline bool authorize(const uint8_t* source,const uint8_t* packet,size_t n) {
  Session copy;
  {
    SessionGuard guard;Session* s=session(source);
    if(!s || !s->used)return false;
    copy=*s;
  }
  if(!verify(packet,n,copy.key,copy.nonce,copy.sequence))return false;
  SessionGuard guard;Session* s=session(source);
  if(!s || memcmp(s->nonce,copy.nonce,8) || s->sequence>=copy.sequence)return false;
  s->sequence=copy.sequence;return true;
}
}
