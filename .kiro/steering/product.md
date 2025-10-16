# Product Overview

## OPC UA to HTTP Bridge

This project is an intelligent gateway that bridges OPC UA industrial automation servers with modern web applications through HTTP REST APIs. The system implements smart caching and on-demand subscription mechanisms to optimize performance and reduce network overhead.

### Core Value Proposition

- **Smart Caching**: Automatically caches OPC UA data points in memory using thread-safe map structures
- **On-Demand Subscriptions**: Creates OPC UA subscriptions only when web clients first request specific data points
- **Auto-Recovery**: Automatically reconnects to OPC UA servers and restores all active subscriptions after connection failures
- **High Performance**: Subsequent requests return cached data instantly without additional OPC UA server calls

### Key Features

- RESTful API endpoint `/iotgateway/read` for reading multiple OPC UA node values
- Environment variable-based configuration for deployment flexibility
- Multiple authentication methods (API Key, Basic Auth)
- CORS support for web application integration
- Automatic subscription cleanup for unused data points
- Comprehensive error handling and status reporting

### Target Use Cases

- Industrial IoT gateways connecting legacy OPC UA systems to modern web dashboards
- Real-time data visualization applications requiring low-latency access to industrial data
- System integration scenarios where HTTP/JSON is preferred over native OPC UA protocols