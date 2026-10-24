/*
HOST_HTTPS.C

HTTPS GET for the host, over the console's own TLS (the ssl service), so the
build carries no TLS library: the updater (host_update.c) fetches through
here.

The community server's requests (host_https_request, the guest's
update_request: port/linux/src/update.h) come here too: a GET or a POST
with its headers and body, the answer kept in memory whatever its status.

A request is HTTP/1.0, so that no answer comes chunked: every server asked
gives a Content-Length, or ends the body by closing. The body goes to the
caller in pieces as it arrives, which lets the updater unpack a release while
it downloads instead of keeping the archive. Redirects are followed when asked
for (GitHub's downloads go through two), or handed back (its latest release is
named by where /releases/latest redirects to).

The roots the hosts' chains end at are imported into each connection, so a
fetch does not depend on which roots this console's firmware carries: ISRG
Root X1 and X2 (halo.milenko.org, GitHub's release assets, through Let's
Encrypt's cross-signed intermediates), USERTrust ECC and RSA (github.com,
through Sectigo), and GTS Root R1 and R4 and GlobalSign Root CA, which R4 is
cross-signed by (api.watanoge.com, the community server, through Google
Trust Services' WE1).

Every answer's Date header is kept with this console's clock at the time
(host_https_clock_offset): the servers' clocks are right, and this one may
not be - consoles that never reach Nintendo never set theirs - which internet
play's server browser needs (host_update.c says so to the player, and the
game is told: host_main.c).
*/

#include "host.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <switch.h>

#define HTTPS_TIMEOUT_SECONDS 15
#define HTTPS_REDIRECTS 5

enum
{
	MAXIMUM_HOST = 255,
	MAXIMUM_HEADERS = 16 * 1024,
	READ_SIZE = 64 * 1024,
};

static const char roots[] =
	"-----BEGIN CERTIFICATE-----\n"
	"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
	"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
	"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
	"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
	"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
	"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
	"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
	"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
	"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
	"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
	"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
	"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
	"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
	"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
	"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
	"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
	"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
	"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
	"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
	"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
	"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
	"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
	"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
	"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
	"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
	"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
	"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
	"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
	"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
	"CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
	"R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
	"MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
	"ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
	"EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
	"+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
	"ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
	"AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
	"zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
	"tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
	"/q4AaOeMSQ+2b1tbFfLn\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
	"MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
	"eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
	"JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
	"MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
	"Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
	"VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
	"aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
	"I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
	"o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
	"A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
	"VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
	"zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
	"RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIIF3jCCA8agAwIBAgIQAf1tMPyjylGoG7xkDjUDLTANBgkqhkiG9w0BAQwFADCB\n"
	"iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n"
	"cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n"
	"BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAw\n"
	"MjAxMDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNV\n"
	"BAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVU\n"
	"aGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBSU0EgQ2Vy\n"
	"dGlmaWNhdGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK\n"
	"AoICAQCAEmUXNg7D2wiz0KxXDXbtzSfTTK1Qg2HiqiBNCS1kCdzOiZ/MPans9s/B\n"
	"3PHTsdZ7NygRK0faOca8Ohm0X6a9fZ2jY0K2dvKpOyuR+OJv0OwWIJAJPuLodMkY\n"
	"tJHUYmTbf6MG8YgYapAiPLz+E/CHFHv25B+O1ORRxhFnRghRy4YUVD+8M/5+bJz/\n"
	"Fp0YvVGONaanZshyZ9shZrHUm3gDwFA66Mzw3LyeTP6vBZY1H1dat//O+T23LLb2\n"
	"VN3I5xI6Ta5MirdcmrS3ID3KfyI0rn47aGYBROcBTkZTmzNg95S+UzeQc0PzMsNT\n"
	"79uq/nROacdrjGCT3sTHDN/hMq7MkztReJVni+49Vv4M0GkPGw/zJSZrM233bkf6\n"
	"c0Plfg6lZrEpfDKEY1WJxA3Bk1QwGROs0303p+tdOmw1XNtB1xLaqUkL39iAigmT\n"
	"Yo61Zs8liM2EuLE/pDkP2QKe6xJMlXzzawWpXhaDzLhn4ugTncxbgtNMs+1b/97l\n"
	"c6wjOy0AvzVVdAlJ2ElYGn+SNuZRkg7zJn0cTRe8yexDJtC/QV9AqURE9JnnV4ee\n"
	"UB9XVKg+/XRjL7FQZQnmWEIuQxpMtPAlR1n6BB6T1CZGSlCBst6+eLf8ZxXhyVeE\n"
	"Hg9j1uliutZfVS7qXMYoCAQlObgOK6nyTJccBz8NUvXt7y+CDwIDAQABo0IwQDAd\n"
	"BgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rIDZsswDgYDVR0PAQH/BAQDAgEGMA8G\n"
	"A1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAFzUfA3P9wF9QZllDHPF\n"
	"Up/L+M+ZBn8b2kMVn54CVVeWFPFSPCeHlCjtHzoBN6J2/FNQwISbxmtOuowhT6KO\n"
	"VWKR82kV2LyI48SqC/3vqOlLVSoGIG1VeCkZ7l8wXEskEVX/JJpuXior7gtNn3/3\n"
	"ATiUFJVDBwn7YKnuHKsSjKCaXqeYalltiz8I+8jRRa8YFWSQEg9zKC7F4iRO/Fjs\n"
	"8PRF/iKz6y+O0tlFYQXBl2+odnKPi4w2r78NBc5xjeambx9spnFixdjQg3IM8WcR\n"
	"iQycE0xyNN+81XHfqnHd4blsjDwSXWXavVcStkNr/+XeTWYRUc+ZruwXtuhxkYze\n"
	"Sf7dNXGiFSeUHM9h4ya7b6NnJSFd5t0dCy5oGzuCr+yDZ4XUmFF0sbmZgIn/f3gZ\n"
	"XHlKYC6SQK5MNyosycdiyA5d9zZbyuAlJQG03RoHnHcAP9Dc1ew91Pq7P8yF1m9/\n"
	"qS3fuQL39ZeatTXaw2ewh0qpKJ4jjv9cJ2vhsE/zB+4ALtRZh8tSQZXq9EfX7mRB\n"
	"VXyNWQKV3WKdwrnuWih0hKWbt5DHDAff9Yk2dDLWKMGwsAvgnEzDHNb842m1R0aB\n"
	"L6KCq9NjRHDEjf8tM7qtj3u1cIiuPhnPQCjY/MiQu12ZIvVS5ljFH4gxQ+6IHdfG\n"
	"jjxDah2nGN59PRbxYvnKkKj9\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw\n"
	"CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU\n"
	"MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw\n"
	"MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp\n"
	"Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA\n"
	"A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo\n"
	"27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w\n"
	"Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw\n"
	"TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl\n"
	"qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH\n"
	"szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8\n"
	"Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk\n"
	"MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92\n"
	"wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p\n"
	"aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN\n"
	"VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID\n"
	"AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E\n"
	"FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb\n"
	"C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe\n"
	"QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy\n"
	"h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4\n"
	"7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J\n"
	"ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef\n"
	"MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/\n"
	"Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT\n"
	"6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ\n"
	"0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm\n"
	"2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb\n"
	"bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD\n"
	"VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG\n"
	"A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw\n"
	"WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz\n"
	"IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi\n"
	"AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi\n"
	"QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR\n"
	"HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW\n"
	"BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D\n"
	"9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8\n"
	"p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD\n"
	"-----END CERTIFICATE-----\n"
	"-----BEGIN CERTIFICATE-----\n"
	"MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n"
	"A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
	"b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n"
	"MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
	"YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n"
	"aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n"
	"jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n"
	"xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n"
	"1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n"
	"snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n"
	"U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n"
	"9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n"
	"BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n"
	"AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n"
	"yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n"
	"38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n"
	"AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n"
	"DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n"
	"HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n"
	"-----END CERTIFICATE-----\n";

static int fail(char *error, size_t error_size, const char *format, ...) __attribute__((format(printf, 3, 4)));

static int fail(char *error, size_t error_size, const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(error, error_size, format, arguments);
	va_end(arguments);
	host_logf(HOST_LOG_WARN, "https: %s", error);
	return -1;
}

static int connect_to(const char *host, char *error, size_t error_size)
{
	struct addrinfo hints;
	struct addrinfo *addresses = NULL;
	struct addrinfo *address;
	struct timeval timeout = { HTTPS_TIMEOUT_SECONDS, 0 };
	int descriptor = -1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, "443", &hints, &addresses) != 0 || !addresses)
	{
		fail(error, error_size, "Cannot look up %s. Is the console connected to the internet?", host);
		return -1;
	}
	for (address = addresses; address && descriptor < 0; address = address->ai_next)
	{
		descriptor = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (descriptor < 0)
			continue;
		setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		if (connect(descriptor, address->ai_addr, address->ai_addrlen) != 0)
		{
			close(descriptor);
			descriptor = -1;
		}
	}
	freeaddrinfo(addresses);
	if (descriptor < 0)
		fail(error, error_size, "Cannot reach %s (%s).", host, strerror(errno));
	return descriptor;
}

/* a header's value, from the block of headers (which ends at end) */
static int header_value(const char *headers, const char *end, const char *name, char *value, size_t size)
{
	size_t length = strlen(name);
	const char *line;

	for (line = headers; line && line < end; )
	{
		const char *next = strstr(line, "\r\n");

		if (!next || next > end)
			next = end;
		if ((size_t)(next - line) > length && !strncasecmp(line, name, length) && line[length] == ':')
		{
			const char *start = line + length + 1;
			size_t copied;

			while (start < next && *start == ' ')
				start++;
			copied = (size_t)(next - start) < size - 1 ? (size_t)(next - start) : size - 1;
			memcpy(value, start, copied);
			value[copied] = 0;
			return 1;
		}
		line = next < end ? next + 2 : NULL;
	}
	return 0;
}

/* how far the servers' clock is ahead of this console's, in seconds, from
the last answer that had a Date */
static int clock_known;
static long long clock_offset;

/* a month's name (Date's, RFC 9110's IMF-fixdate), 0 to 11, or -1 */
static int month_index(const char *name)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	int index;

	for (index = 0; index < 12; index++)
	{
		if (!strncmp(name, months + 3 * index, 3))
			return index;
	}
	return -1;
}

/* "Thu, 23 Oct 2026 04:19:03 GMT" as seconds since 1970 (the days from the
civil date, as Howard Hinnant's days_from_civil), 0 if it is not one */
static int date_seconds(const char *text, long long *seconds)
{
	char month_name[4];
	int day, year, hour, minute, second, month;
	long long days, era, of_era, of_year;

	if (sscanf(text, "%*3s, %d %3s %d %d:%d:%d GMT", &day, month_name, &year, &hour, &minute, &second) != 6)
		return 0;
	month = month_index(month_name);
	if (month < 0 || day < 1 || day > 31 || year < 1970 || hour > 23 || minute > 59 || second > 60)
		return 0;
	month++;
	if (month <= 2)
		year--;
	era = year / 400;
	of_era = year - era * 400;
	of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
	days = era * 146097 + (of_era * 365 + of_era / 4 - of_era / 100 + of_year) - 719468;
	*seconds = days * 86400 + hour * 3600 + minute * 60 + second;
	return 1;
}

int host_https_clock_offset(long long *seconds)
{
	if (clock_known)
		*seconds = clock_offset;
	return clock_known;
}

/* one request of path on host (method, with the header lines in headers and
the text in payload, either NULL for none): the status, the redirect's target
if there is one, and on 200 (or whatever the status, with any_status) the
body to body(). -1 if it could not be asked. */
static int request(const char *host, const char *path, const char *method, const char *headers,
	const char *payload, int any_status, host_https_body body, void *context, char *location,
	size_t location_size, char *error, size_t error_size)
{
	SslContext ssl_context;
	SslConnection connection;
	int have_ssl = 0, have_context = 0, have_connection = 0;
	int descriptor;
	int status = -1;
	char *buffer = NULL;
	size_t length = 0;
	Result result;

	if (location_size)
		location[0] = 0;
	descriptor = connect_to(host, error, error_size);
	if (descriptor < 0)
		return -1;
	buffer = malloc(READ_SIZE > MAXIMUM_HEADERS ? READ_SIZE : MAXIMUM_HEADERS);
	if (!buffer)
	{
		fail(error, error_size, "Out of memory for the download.");
		goto done;
	}
	result = sslInitialize(1);
	if (R_FAILED(result))
	{
		fail(error, error_size, "The console's TLS service is not available (0x%08x).", (unsigned)result);
		goto done;
	}
	have_ssl = 1;
	result = sslCreateContext(&ssl_context, SslVersion_Auto);
	if (R_FAILED(result))
	{
		fail(error, error_size, "Cannot set up TLS (0x%08x).", (unsigned)result);
		goto done;
	}
	have_context = 1;
	/* not fatal: the firmware's own roots may be enough */
	result = sslContextImportServerPki(&ssl_context, roots, sizeof(roots), SslCertificateFormat_Pem, NULL);
	if (R_FAILED(result))
		host_logf(HOST_LOG_WARN, "https: could not import the roots (0x%08x)", (unsigned)result);
	result = sslContextCreateConnection(&ssl_context, &connection);
	if (R_FAILED(result))
	{
		fail(error, error_size, "Cannot set up TLS (0x%08x).", (unsigned)result);
		goto done;
	}
	have_connection = 1;
	/* the socket stays this file's to close, whatever the service does */
	sslConnectionSetOption(&connection, SslOptionType_DoNotCloseSocket, true);
	if (socketSslConnectionSetSocketDescriptor(&connection, descriptor) < 0 && errno != ENOENT)
	{
		fail(error, error_size, "Cannot hand the connection to TLS (%s).", strerror(errno));
		goto done;
	}
	sslConnectionSetHostName(&connection, host, (u32)strlen(host));
	result = sslConnectionDoHandshake(&connection, NULL, NULL, NULL, 0);
	if (R_FAILED(result))
	{
		fail(error, error_size, "The secure connection to %s failed (0x%08x).", host, (unsigned)result);
		goto done;
	}
	{
		size_t payload_size = payload ? strlen(payload) : 0;
		char *text = malloc(strlen(path) + strlen(host) + (headers ? strlen(headers) : 0) + payload_size + 320);
		size_t sent = 0, size;

		if (!text)
		{
			fail(error, error_size, "Out of memory for the request.");
			goto done;
		}
		size = (size_t)sprintf(text, "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: halo-ce-universal-switch\r\n"
			"Accept: */*\r\nConnection: close\r\n%s", method, path, host, headers ? headers : "");
		if (payload)
			size += (size_t)sprintf(text + size, "Content-Length: %lu\r\n\r\n%s", (unsigned long)payload_size, payload);
		else
			size += (size_t)sprintf(text + size, "\r\n");
		while (sent < size)
		{
			u32 written = 0;

			result = sslConnectionWrite(&connection, text + sent, (u32)(size - sent), &written);
			if (R_FAILED(result) || !written)
				break;
			sent += written;
		}
		free(text);
		if (sent < size)
		{
			fail(error, error_size, "Cannot send the request to %s (0x%08x).", host, (unsigned)result);
			goto done;
		}
	}
	/* the headers, whole */
	{
		char *end = NULL;
		long long content_length = -1, delivered = 0;
		char value[64];

		while (!end && length < MAXIMUM_HEADERS - 1)
		{
			u32 received = 0;

			result = sslConnectionRead(&connection, buffer + length, (u32)(MAXIMUM_HEADERS - 1 - length), &received);
			if (R_FAILED(result) || !received)
				break;
			length += received;
			buffer[length] = 0;
			end = strstr(buffer, "\r\n\r\n");
		}
		if (!end || strncmp(buffer, "HTTP/1.", 7))
		{
			fail(error, error_size, "%s sent no answer.", host);
			goto done;
		}
		status = atoi(buffer + 9);
		if (header_value(buffer, end, "Date", value, sizeof(value)))
		{
			long long server_time;

			if (date_seconds(value, &server_time))
			{
				clock_offset = server_time - (long long)time(NULL);
				clock_known = 1;
			}
		}
		if (location_size)
			header_value(buffer, end, "Location", location, location_size);
		if (header_value(buffer, end, "Content-Length", value, sizeof(value)))
			content_length = strtoll(value, NULL, 10);
		if ((status != 200 && !any_status) || !body)
			goto done;
		/* what came with the headers, then the rest */
		{
			size_t start = (size_t)(end + 4 - buffer);

			if (length > start)
			{
				if (!body(context, buffer + start, length - start, content_length))
				{
					status = -1;
					fail(error, error_size, "The download was stopped.");
					goto done;
				}
				delivered += (long long)(length - start);
			}
		}
		while (content_length < 0 || delivered < content_length)
		{
			u32 received = 0;

			result = sslConnectionRead(&connection, buffer, READ_SIZE, &received);
			if (R_FAILED(result) || !received)
				break;
			if (!body(context, buffer, received, content_length))
			{
				status = -1;
				fail(error, error_size, "The download was stopped.");
				goto done;
			}
			delivered += received;
		}
		if (content_length >= 0 && delivered < content_length)
		{
			status = -1;
			fail(error, error_size, "The download from %s was cut off (%lld of %lld bytes).", host, delivered,
				content_length);
		}
	}

done:
	if (have_connection)
		sslConnectionClose(&connection);
	if (have_context)
		sslContextClose(&ssl_context);
	if (have_ssl)
		sslExit();
	close(descriptor);
	free(buffer);
	return status;
}

/* https://host/path into its parts */
static int split_url(const char *url, char *host, size_t host_size, const char **path)
{
	const char *start, *slash;

	if (strncmp(url, "https://", 8))
		return 0;
	start = url + 8;
	slash = strchr(start, '/');
	if (!slash || (size_t)(slash - start) >= host_size || slash == start)
		return 0;
	memcpy(host, start, (size_t)(slash - start));
	host[slash - start] = 0;
	*path = slash;
	return 1;
}

int host_https_get(const char *url, int follow_redirects, host_https_body body, void *context, char *location,
	size_t location_size, char *error, size_t error_size)
{
	char host[MAXIMUM_HOST + 1];
	char *target = malloc(HOST_HTTPS_MAXIMUM_URL);
	char *next = malloc(HOST_HTTPS_MAXIMUM_URL);
	const char *path;
	int redirects, status = -1;

	if (!target || !next)
	{
		free(target);
		free(next);
		return fail(error, error_size, "Out of memory for the request.");
	}
	snprintf(target, HOST_HTTPS_MAXIMUM_URL, "%s", url);
	for (redirects = 0; redirects <= HTTPS_REDIRECTS; redirects++)
	{
		if (!split_url(target, host, sizeof(host), &path))
		{
			status = fail(error, error_size, "Not an https link: %.80s", target);
			break;
		}
		status = request(host, path, "GET", NULL, NULL, 0, body, context, next, HOST_HTTPS_MAXIMUM_URL, error,
			error_size);
		if (status < 0 || status < 300 || status >= 400 || !next[0])
			break;
		if (!follow_redirects)
			break;
		/* a path on the same host, or a whole link */
		if (next[0] == '/')
		{
			char *joined = malloc(HOST_HTTPS_MAXIMUM_URL);

			if (!joined)
			{
				status = fail(error, error_size, "Out of memory for the request.");
				break;
			}
			snprintf(joined, HOST_HTTPS_MAXIMUM_URL, "https://%s%s", host, next);
			snprintf(target, HOST_HTTPS_MAXIMUM_URL, "%s", joined);
			free(joined);
		}
		else
			snprintf(target, HOST_HTTPS_MAXIMUM_URL, "%s", next);
	}
	if (location_size)
		snprintf(location, location_size, "%s", next);
	free(target);
	free(next);
	return status;
}

/* ---------- the guest's update_request (port/android/guest/runtime/guest_https.c) */

struct memory_body
{
	char *response;
	size_t size, length;
};

static int memory_body(void *context, const void *data, size_t size, long long total)
{
	struct memory_body *memory = context;

	(void)total;
	/* (an answer larger than its room is refused, not cut) */
	if (size > memory->size - 1 - memory->length)
		return 0;
	memcpy(memory->response + memory->length, data, size);
	memory->length += size;
	memory->response[memory->length] = 0;
	return 1;
}

int host_https_request(int post, const char *url, const char *headers, const char *body, char *response,
	int response_size)
{
	char host[MAXIMUM_HOST + 1];
	char error[256] = "";
	const char *path;
	struct memory_body memory;
	int status;

	if (!response || response_size <= 0)
		return 0;
	response[0] = 0;
	if (!split_url(url, host, sizeof(host), &path))
	{
		snprintf(response, (size_t)response_size, "Not an https link: %.80s", url);
		return 0;
	}
	memory.response = response;
	memory.size = (size_t)response_size;
	memory.length = 0;
	status = request(host, path, post ? "POST" : "GET", headers, post ? (body ? body : "") : NULL, 1, memory_body,
		&memory, NULL, 0, error, sizeof(error));
	/* (why, in the answer's room: guest_https.c moves it to its caller's) */
	if (status <= 0)
	{
		snprintf(response, (size_t)response_size, "%s", error[0] ? error : "no answer");
		return 0;
	}
	return status;
}
