#pragma once 

#if defined(_WIN32)

#pragma pack(push, 1)

typedef struct iphdr
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned char  ihl: 4;        //
    unsigned char  version: 4;    // 4-bit IPv4 version
    // 4-bit header length (in 32-bit words)
#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned char  version: 4;    // 4-bit IPv4 version
    // 4-bit header length (in 32-bit words)
    unsigned char  ihl: 4;        //
#else
# error "Your systems ENDIANNESS is broken, please fix!"
#endif

    std::uint8_t    tos;           // IP type of service
    std::uint16_t   tot_len;       // Total length
    std::uint16_t   id;            // Unique identifier
    std::uint16_t   offset;        // Fragment offset field
    std::uint8_t    ttl;           // Time to live
    std::uint8_t    protocol;      // Protocol(TCP,UDP etc)
    std::uint16_t   check;         // IP checksum
    std::uint32_t   saddr;         // Source address
    std::uint32_t   daddr;         // Source address
} IPV4_HDR, *PIPV4_HDR, FAR *LPIPV4_HDR;

// IPv4 option header
typedef struct ipv4_option_hdr
{
    std::uint8_t   opt_code;           // option type
    std::uint8_t   opt_len;            // length of the option header
    std::uint8_t   opt_ptr;            // offset into options
    std::uint32_t  opt_addr[9];        // list of IPv4 addresses
} IPV4_OPTION_HDR, *PIPV4_OPTION_HDR, FAR *LPIPV4_OPTION_HDR;

// ICMPv6 echo request body
typedef struct icmpv6_echo_request
{
    struct
    {
        std::uint16_t  id;
        std::uint16_t  sequence;
    } echo;
} ICMPV6_ECHO_REQUEST;

// ICMP header
typedef struct icmphdr
{
    std::uint8_t   type;
    std::uint8_t   code;
    std::uint16_t  checksum;
    std::uint16_t  id;
    std::uint16_t  sequence;
    std::uint32_t  timestamp;
    icmpv6_echo_request un;
} ICMP_HDR, *PICMP_HDR, FAR *LPICMP_HDR;

// IPv6 protocol header
typedef struct ipv6_hdr
{
    std::uint32_t   ipv6_vertcflow;        // 4-bit IPv6 version
                                           // 8-bit traffic class
                                           // 20-bit flow label
    std::uint16_t   ipv6_payloadlen;       // payload length
    std::uint8_t    ipv6_nexthdr;          // next header protocol value
    std::uint8_t    ipv6_hoplimit;         // TTL
    struct in6_addr ipv6_srcaddr;          // Source address
    struct in6_addr ipv6_destaddr;         // Destination address
} IPV6_HDR, *PIPV6_HDR, FAR *LPIPV6_HDR;

// IPv6 fragment header
typedef struct ipv6_fragment_hdr
{
    std::uint8_t   ipv6_frag_nexthdr;
    std::uint8_t   ipv6_frag_reserved;
    std::uint16_t  ipv6_frag_offset;
    std::uint32_t  ipv6_frag_id;
} IPV6_FRAGMENT_HDR, *PIPV6_FRAGMENT_HDR, FAR *LPIPV6_FRAGMENT_HDR;

// ICMPv6 header
typedef struct icmpv6_hdr
{
    std::uint8_t   icmp6_type;
    std::uint8_t   icmp6_code;
    std::uint16_t  icmp6_checksum;
} ICMPV6_HDR;

// Define the UDP header
typedef struct udp_hdr
{
    std::uint16_t src_portno;       // Source port no.
    std::uint16_t dst_portno;       // Dest. port no.
    std::uint16_t udp_length;       // Udp packet length
    std::uint16_t udp_checksum;     // Udp checksum (optional)
} UDP_HDR, *PUDP_HDR;

#define IP_RECORD_ROUTE     0x7

// ICMP6 protocol value
#define IPPROTO_ICMP6       58

// ICMP types and codes
#define ICMPV4_ECHO_REQUEST_TYPE   8
#define ICMPV4_ECHO_REQUEST_CODE   0
#define ICMPV4_ECHO_REPLY_TYPE     0
#define ICMPV4_ECHO_REPLY_CODE     0

#define ICMPV4_DESTUNREACH    3
#define ICMPV4_SRCQUENCH      4
#define ICMPV4_REDIRECT       5
#define ICMP_ECHO             8
#define ICMPV4_TIMEOUT       11
#define ICMPV4_PARMERR       12

// ICMP6 types and codes
#define ICMPV6_ECHO_REQUEST_TYPE   128
#define ICMPV6_ECHO_REQUEST_CODE   0
#define ICMPV6_ECHO_REPLY_TYPE     129
#define ICMPV6_ECHO_REPLY_CODE     0
#define ICMPV6_TIME_EXCEEDED_TYPE  3
#define ICMPV6_TIME_EXCEEDED_CODE  0

#pragma pack(pop)

#endif
