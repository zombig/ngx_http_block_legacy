/*
 * nginx module to block legacy HTTP versions (HTTP/1.0, HTTP/1.1)
 * Supports flexible configuration with individual protocol control
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_flag_t  enable;
    ngx_flag_t  block_http10;
    ngx_flag_t  block_http11;
    ngx_flag_t  block_http09;
    ngx_str_t   custom_message;
} ngx_http_block_legacy_conf_t;

static ngx_int_t ngx_http_block_legacy_handler(ngx_http_request_t *r);
static void *ngx_http_block_legacy_create_conf(ngx_conf_t *cf);
static char *ngx_http_block_legacy_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_block_legacy_init(ngx_conf_t *cf);
static char *ngx_http_block_legacy_custom_message(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_command_t ngx_http_block_legacy_commands[] = {
    {
        ngx_string("block_legacy_http"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_block_legacy_conf_t, enable),
        NULL
    },
    {
        ngx_string("block_http10"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_block_legacy_conf_t, block_http10),
        NULL
    },
    {
        ngx_string("block_http11"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_block_legacy_conf_t, block_http11),
        NULL
    },
    {
        ngx_string("block_http09"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_block_legacy_conf_t, block_http09),
        NULL
    },
    {
        ngx_string("legacy_http_message"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
        ngx_http_block_legacy_custom_message,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    ngx_null_command
};

static ngx_http_module_t ngx_http_block_legacy_module_ctx = {
    NULL,                                    /* preconfiguration */
    ngx_http_block_legacy_init,             /* postconfiguration */
    NULL,                                    /* create main configuration */
    NULL,                                    /* init main configuration */
    NULL,                                    /* create server configuration */
    NULL,                                    /* merge server configuration */
    ngx_http_block_legacy_create_conf,      /* create location configuration */
    ngx_http_block_legacy_merge_conf        /* merge location configuration */
};

ngx_module_t ngx_http_block_legacy_module = {
    NGX_MODULE_V1,
    &ngx_http_block_legacy_module_ctx,      /* module context */
    ngx_http_block_legacy_commands,         /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                    /* init master */
    NULL,                                    /* init module */
    NULL,                                    /* init process */
    NULL,                                    /* init thread */
    NULL,                                    /* exit thread */
    NULL,                                    /* exit process */
    NULL,                                    /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_block_legacy_handler(ngx_http_request_t *r)
{
    ngx_http_block_legacy_conf_t *conf;
    ngx_int_t should_block = 0;
    ngx_str_t blocked_version = ngx_null_string;
    ngx_str_t response_body;
    ngx_buf_t *b;
    ngx_chain_t out;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_block_legacy_module);

    /* Check if module is enabled */
    if (!conf->enable) {
        return NGX_DECLINED;
    }

    /* Check which HTTP version to block */
    switch (r->http_version) {
        case NGX_HTTP_VERSION_9:
            if (conf->block_http09) {
                should_block = 1;
                ngx_str_set(&blocked_version, "HTTP/0.9");
            }
            break;

        case NGX_HTTP_VERSION_10:
            if (conf->block_http10) {
                should_block = 1;
                ngx_str_set(&blocked_version, "HTTP/1.0");
            }
            break;

        case NGX_HTTP_VERSION_11:
            if (conf->block_http11) {
                should_block = 1;
                ngx_str_set(&blocked_version, "HTTP/1.1");
            }
            break;

        default:
            /* HTTP/2.0+ are allowed */
            return NGX_DECLINED;
    }

    if (!should_block) {
        return NGX_DECLINED;
    }

    /* Log blocked request */
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                 "%V request blocked by security policy, "
                 "client: %V, request: \"%V\"",
                 &blocked_version, &r->connection->addr_text,
                 &r->request_line);

    /* Prepare response */
    r->headers_out.status = 426;  /* 426 Upgrade Required */
    r->headers_out.content_length_n = -1;

    /* Add required headers for 426 response */
    ngx_table_elt_t *upgrade_header = ngx_list_push(&r->headers_out.headers);
    if (upgrade_header != NULL) {
        upgrade_header->hash = 1;
        ngx_str_set(&upgrade_header->key, "Upgrade");
        ngx_str_set(&upgrade_header->value, "HTTP/2.0, HTTP/1.1");
    }

    ngx_table_elt_t *connection_header = ngx_list_push(&r->headers_out.headers);
    if (connection_header != NULL) {
        connection_header->hash = 1;
        ngx_str_set(&connection_header->key, "Connection");
        ngx_str_set(&connection_header->value, "Upgrade");
    }

    /* Send headers */
    ngx_int_t rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    /* Prepare response body */
    if (conf->custom_message.len > 0) {
        response_body = conf->custom_message;
    } else {
        /* Default message */
        u_char *default_msg = (u_char*)
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>426 Upgrade Required</title></head>\n"
            "<body>\n"
            "<center><h1>426 Upgrade Required</h1></center>\n"
            "<hr>\n"
            "<center>This server requires HTTP/2.0 or HTTP/1.1</center>\n"
            "<center>Your client used: ";

        size_t total_len = ngx_strlen(default_msg) + blocked_version.len +
                          ngx_strlen("</center>\n</body>\n</html>\n");

        u_char *full_msg = ngx_pnalloc(r->pool, total_len);
        if (full_msg == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        u_char *p = full_msg;
        p = ngx_cpymem(p, default_msg, ngx_strlen(default_msg));
        p = ngx_cpymem(p, blocked_version.data, blocked_version.len);
        p = ngx_cpymem(p, "</center>\n</body>\n</html>\n",
                      ngx_strlen("</center>\n</body>\n</html>\n"));

        response_body.data = full_msg;
        response_body.len = total_len;
    }

    /* Create buffer for response */
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = response_body.data;
    b->last = response_body.data + response_body.len;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    r->headers_out.content_length_n = response_body.len;

    return ngx_http_output_filter(r, &out);
}

static void *
ngx_http_block_legacy_create_conf(ngx_conf_t *cf)
{
    ngx_http_block_legacy_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_block_legacy_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    conf->block_http10 = NGX_CONF_UNSET;
    conf->block_http11 = NGX_CONF_UNSET;
    conf->block_http09 = NGX_CONF_UNSET;

    return conf;
}

static char *
ngx_http_block_legacy_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_block_legacy_conf_t *prev = parent;
    ngx_http_block_legacy_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_value(conf->block_http10, prev->block_http10, 1);
    ngx_conf_merge_value(conf->block_http11, prev->block_http11, 0);
    ngx_conf_merge_value(conf->block_http09, prev->block_http09, 1);

    /* If module is explicitly disabled, override all blocking */
    if (conf->enable == 0) {
        conf->block_http09 = 0;
        conf->block_http10 = 0;
        conf->block_http11 = 0;
    }

    ngx_conf_merge_str_value(conf->custom_message, prev->custom_message, "");

    return NGX_CONF_OK;
}

static char *
ngx_http_block_legacy_custom_message(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_block_legacy_conf_t *blcf = conf;
    ngx_str_t *value;

    if (blcf->custom_message.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;
    blcf->custom_message = value[1];

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_block_legacy_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_block_legacy_handler;

    return NGX_OK;
}
