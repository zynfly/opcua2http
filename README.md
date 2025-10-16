# OPC UA to HTTP

## introduction

this project use open62541 to connect opc ua server.
and use crow to serve http.
and use nlohmann-json to parse json.

## usage

Read OPC UA values (/iotgateway)

```
GET /iotgateway/read?ids=<node-id1>,<node-id2>,...
```

* Query Parameters:
    * ids (required): One or more OPC UA Node IDs, comma-separated. (E.g., `ns=2;s=MyVariable,ns=3;i=1001`)
* Successful Response (200 OK):
```json
{
	"readResults": [
		{
			"id": "ns=2;s=MyVariable",
			"s": true, // Success (OPC UA status code)
			"r": "Good", // Reason / Status description
			"v": "123.45", // Read value
			"t": 1678886400000 // Timestamp (OPC UA source timestamp)
		}
		// ... more results
	]
}
```

* Error Response (e.g., 400 Bad Request if `ids` is missing):
```json
{
	"error": "Parameter 'ids' is required"
}
```


## config

use environment variables to config.

```
# === Core OPC UA Configuration ===
OPC_ENDPOINT=opc.tcp://127.0.0.1:4840      # OPC UA Server URL
OPC_SECURITY_MODE=1                        # 1:None, 2:Sign, 3:SignAndEncrypt
OPC_SECURITY_POLICY=None                   # None, Basic128Rsa15, Basic256, Basic256Sha256, Aes128_Sha256_RsaOaep, Aes256_Sha256_RsaPss
OPC_NAMESPACE=2                            # Default namespace for Node IDs (if not specified)
OPC_APPLICATION_URI=urn:CLIENT:NodeOPCUA-Client # Client application URI

# === OPC UA Connection Configuration ===
CONNECTION_RETRY_MAX=5                     # Max retries per connection attempt
CONNECTION_INITIAL_DELAY=1000              # Initial delay before first attempt (ms)
CONNECTION_MAX_RETRY=10                    # Global max reconnection attempts (-1 for infinite)
CONNECTION_MAX_DELAY=10000                 # Max delay between retries (ms)
CONNECTION_RETRY_DELAY=5000                # Base delay between retries (ms)

# === Web Server Configuration ===
SERVER_PORT=3000                           # Port the gateway will listen on

# === API Security Configuration ===
API_KEY=your_api_key_here                  # Secret key for X-API-Key authentication
AUTH_USERNAME=admin                        # User for Basic Authentication
AUTH_PASSWORD=your_secure_password         # Password for Basic Authentication
ALLOWED_ORIGINS=http://localhost:3000,[https://your-frontend-domain.com](https://your-frontend-domain.com) # Allowed CORS origins (comma-separated)
```

## other

### build

this project use cmake to build.
and use vcpkg to manage dependencies.
when build, you should add
```
-DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
```
