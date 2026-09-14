#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef GOTEK_AUTH_HOST_TEST
#include <openssl/hmac.h>
#else
#include <mbedtls/md.h>
#endif

// Authentication footer in the existing 250-byte ESP-NOW control frame:
// nonce[8], sequence LE32, HMAC-SHA256[32]. All preceding bytes are covered.
namespace GotekAuth {
constexpr size_t packetSize=250, nonceOffset=206, seqOffset=214, tagOffset=218;
inline uint32_t sequence(const uint8_t* p) {
  return uint32_t(p[214]) | (uint32_t(p[215])<<8) |
    (uint32_t(p[216])<<16) | (uint32_t(p[217])<<24);
}
inline bool digest(const uint8_t* key,const uint8_t* p,uint8_t* tag) {
#ifdef GOTEK_AUTH_HOST_TEST
  unsigned n=32;
  return HMAC(EVP_sha256(),key,16,p,tagOffset,tag,&n) && n==32;
#else
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         key,16,p,tagOffset,tag)==0;
#endif
}
inline bool seal(uint8_t* p,size_t n,const uint8_t* key,
                 const uint8_t* nonce,uint32_t seq) {
  if(n!=packetSize)return false;
  memcpy(p+nonceOffset,nonce,8);
  for(int i=0;i<4;++i)p[seqOffset+i]=uint8_t(seq>>(8*i));
  return digest(key,p,p+tagOffset);
}
inline bool authentic(const uint8_t* p,size_t n,const uint8_t* key) {
  if(n!=packetSize)return false;
  uint8_t tag[32];if(!digest(key,p,tag))return false;
  uint8_t different=0;
  for(size_t i=0;i<32;++i)different|=tag[i]^p[tagOffset+i];
  return different==0;
}
inline bool verify(const uint8_t* p,size_t n,const uint8_t* key,
                   const uint8_t* nonce,uint32_t& last) {
  if(n!=packetSize || memcmp(p+nonceOffset,nonce,8) ||
     !sequence(p) || sequence(p)<=last || !authentic(p,n,key))return false;
  last=sequence(p);return true;
}
}
