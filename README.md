# nginx-block-legacy - NGINX Module for Blocking Legacy HTTP Versions

This NGINX module provides flexible blocking of legacy HTTP versions (HTTP/0.9, HTTP/1.0, HTTP/1.1) with individual protocol control and customizable responses.

## Features

- ✅ **Flexible Protocol Control**: Enable/disable blocking for HTTP/0.9, HTTP/1.0, HTTP/1.1 individually
- ✅ **Safe Defaults**: By default blocks HTTP/0.9 and HTTP/1.0, allows HTTP/1.1 and HTTP/2.0+
- ✅ **Hierarchical Configuration**: Configure globally, per-server, or per-location
- ✅ **Custom Error Messages**: Set custom HTML response for blocked requests
- ✅ **Proper HTTP Compliance**: Returns 426 Upgrade Required with appropriate headers
- ✅ **Security Logging**: Logs all blocked requests with client details
- ✅ **Zero Configuration**: Works out-of-the-box with sensible defaults

## Installation

### Quick Install (Dynamic Module)

```bash
# Clone repository
git clone https://github.com/zombig/ngx_http_block_legacy.git
cd ngx_http_block_legacy

# Build and install
make dynamic
sudo make install-dynamic
```

### Manual Compilation

```bash
# Download nginx source
wget http://nginx.org/download/nginx-1.24.0.tar.gz
tar -xzf nginx-1.24.0.tar.gz

# Configure with module
cd nginx-1.24.0
./configure --add-dynamic-module=/path/to/nginx-block-legacy
make modules

# Install module
sudo cp objs/ngx_http_block_legacy_module.so /etc/nginx/modules/
```

## Configuration

### Enable Module

Add to `/etc/nginx/nginx.conf`:

```nginx
load_module modules/ngx_http_block_legacy_module.so;

http {
    # Global configuration
    block_legacy_http on;

    # Your existing configuration...
}
```

### Configuration Directives

| Directive | Context | Default | Description |
|-----------|---------|---------|-------------|
| `block_legacy_http` | http, server, location | `off` | Enable/disable the module |
| `block_http09` | http, server, location | `on` | Block HTTP/0.9 requests |
| `block_http10` | http, server, location | `on` | Block HTTP/1.0 requests |
| `block_http11` | http, server, location | `off` | Block HTTP/1.1 requests |
| `legacy_http_message` | http, server, location | (default HTML) | Custom error message |

## Usage Examples

### Default Configuration (Recommended)

```nginx
http {
    # Enable with defaults: blocks HTTP/0.9 and HTTP/1.0, allows HTTP/1.1+
    block_legacy_http on;
}
```

### Block All Legacy Versions (HTTP/2.0+ Only)

```nginx
http {
    block_legacy_http on;
    block_http09 on;   # Default
    block_http10 on;   # Default
    block_http11 on;   # Enable HTTP/1.1 blocking
}
```

### Per-Server Configuration

```nginx
http {
    # Default: allow all versions
    block_legacy_http off;

    server {
        server_name api.example.com;
        # API server requires HTTP/2.0+ only
        block_legacy_http on;
        block_http11 on;
    }

    server {
        server_name legacy.example.com;
        # Legacy server allows HTTP/1.0+
        block_legacy_http on;
        block_http10 off;  # Allow HTTP/1.0
    }
}
```

### Per-Location Configuration

```nginx
server {
    server_name example.com;

    # Default: allow HTTP/1.0+
    block_legacy_http on;
    block_http10 off;

    location /api/ {
        # API endpoints require HTTP/1.1+
        block_http10 on;
    }

    location /admin/ {
        # Admin area requires HTTP/2.0+
        block_http10 on;
        block_http11 on;
    }

    location /legacy/ {
        # Legacy endpoint allows all versions
        block_legacy_http off;
    }
}
```

### Custom Error Message

```nginx
http {
    block_legacy_http on;
    legacy_http_message '<!DOCTYPE html>
<html>
<head><title>Protocol Not Supported</title></head>
<body>
<h1>Upgrade Your Client</h1>
<p>This server requires a modern HTTP client supporting HTTP/2.0 or HTTP/1.1.</p>
<p>Please upgrade your software or contact support.</p>
</body>
</html>';
}
```

### Real-World Production Example

```nginx
# /etc/nginx/nginx.conf
load_module modules/ngx_http_block_legacy_module.so;

http {
    # Global security settings
    block_legacy_http on;     # Enable module
    block_http09 on;          # Block HTTP/0.9 (default)
    block_http10 on;          # Block HTTP/1.0 (default)
    block_http11 off;         # Allow HTTP/1.1 (default)

    # Include all server configurations
    include /etc/nginx/conf.d/*.conf;
}
```

```nginx
# /etc/nginx/conf.d/api.conf
server {
    listen 443 ssl http2;
    server_name api.example.com;

    # API requires HTTP/2.0+ for performance
    block_http11 on;

    location / {
        proxy_pass http://backend;
    }
}
```

```nginx
# /etc/nginx/conf.d/main.conf
server {
    listen 443 ssl http2;
    server_name www.example.com;

    # Main site: allow HTTP/1.1+ (default config)

    location /legacy-api/ {
        # Legacy API endpoint allows HTTP/1.0 for old clients
        block_http10 off;
        proxy_pass http://legacy-backend;
    }
}
```

## Security Benefits

### 1. **Prevents SNI Information Disclosure**

HTTP/1.0 with TLS can send SNI without Host header, potentially exposing server IP addresses even behind load balancers:

```bash
# Blocked by module:
openssl s_client -connect example.com:443 -servername example.com
# Then send: GET / HTTP/1.0\r\n\r\n
```

### 2. **Mitigates Protocol Downgrade Attacks**

Prevents attackers from forcing connections to use vulnerable legacy protocols.

### 3. **Reduces Attack Surface**

Legacy protocols have known vulnerabilities and implementation quirks that can be exploited.

## Testing

### Test HTTP/1.0 Blocking

```bash
# Should return 426 Upgrade Required
curl -0 http://example.com/

# Should work normally
curl http://example.com/
```

### Test HTTP/1.1 Blocking

```bash
# Should return 426 if block_http11 on
curl http://example.com/strict

# Should work normally
curl --http2-prior-knowledge https://example.com/
```

### Check Response Headers

```bash
curl -I -0 http://example.com/
# Expected response:
# HTTP/1.1 426 Upgrade Required
# Upgrade: HTTP/2.0, HTTP/1.1
# Connection: Upgrade
```

## Monitoring

### Log Analysis

Blocked requests are logged as WARN level:

```text
2025/07/21 12:00:00 [warn] 1234#0: HTTP/1.0 request blocked by security policy,
client: 192.168.1.100, server: example.com, request: "GET / HTTP/1.0"
```

### Monitoring Script

```bash
#!/bin/bash
# Count blocked requests in the last hour
grep "request blocked by security policy" /var/log/nginx/error.log | \
grep "$(date -d '1 hour ago' '+%Y/%m/%d %H')" | \
wc -l
```

## Migration Guide

### For Existing Deployments

1. **Test in staging first**
2. **Enable module with safe defaults**:

   ```nginx
   block_legacy_http on;  # Only blocks HTTP/0.9 and HTTP/1.0
   ```

3. **Monitor logs for blocked legitimate clients**
4. **Gradually enable stricter blocking if needed**

### Rollback Plan

```nginx
# Disable module completely
block_legacy_http off;

# Or allow all legacy versions
block_legacy_http on;
block_http09 off;
block_http10 off;
block_http11 off;
```

## Performance Impact

- **Minimal CPU overhead**: Simple version check in REWRITE phase after location selection
- **No memory overhead**: No additional allocations for allowed requests
- **Network efficient**: Early blocking prevents unnecessary processing
- **Location-aware**: Executes after location selection for proper per-location configuration

## Compatibility

- **NGINX Version**: 1.9.11+ (dynamic modules)
- **HTTP/2 Support**: Full compatibility
- **Load Balancers**: Works with any upstream proxy
- **CDN/WAF**: Compatible with Cloudflare, AWS ALB, etc.

## Troubleshooting

### Module Not Loading

```bash
# Check module file exists
ls -la /etc/nginx/modules/ngx_http_block_legacy_module.so

# Test nginx configuration
nginx -t

# Check error logs
tail -f /var/log/nginx/error.log
```

### Unexpected Blocking

```bash
# Check effective configuration
nginx -T | grep -A5 -B5 block_legacy

# Test specific protocol version
curl -v -0 http://example.com/
curl -v http://example.com/
curl -v --http2-prior-knowledge https://example.com/
```

## License

MIT License - see LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Submit a pull request

## Support

- GitHub Issues: Report bugs and feature requests
- Documentation: Check this README and inline comments
- Security Issues: Email security@zombig.name
