#define DDEBUG 0
#include "ddebug.h"

#include "ngx_http_echo_util.h"
#include "ngx_http_echo_subrequest.h"
#include "ngx_http_echo_handler.h"

#define ngx_http_echo_method_name(m) { sizeof(m) - 1, (u_char *) m " " }

ngx_str_t  ngx_http_echo_content_length_header_key =
        ngx_string("Content-Length");

ngx_str_t  ngx_http_echo_get_method = ngx_http_echo_method_name("GET");
ngx_str_t  ngx_http_echo_put_method = ngx_http_echo_method_name("PUT");
ngx_str_t  ngx_http_echo_post_method = ngx_http_echo_method_name("POST");
ngx_str_t  ngx_http_echo_head_method = ngx_http_echo_method_name("HEAD");
ngx_str_t  ngx_http_echo_copy_method = ngx_http_echo_method_name("COPY");
ngx_str_t  ngx_http_echo_move_method = ngx_http_echo_method_name("MOVE");
ngx_str_t  ngx_http_echo_lock_method = ngx_http_echo_method_name("LOCK");
ngx_str_t  ngx_http_echo_mkcol_method = ngx_http_echo_method_name("MKCOL");
ngx_str_t  ngx_http_echo_trace_method = ngx_http_echo_method_name("TRACE");
ngx_str_t  ngx_http_echo_delete_method = ngx_http_echo_method_name("DELETE");
ngx_str_t  ngx_http_echo_unlock_method = ngx_http_echo_method_name("UNLOCK");
ngx_str_t  ngx_http_echo_options_method = ngx_http_echo_method_name("OPTIONS");
ngx_str_t  ngx_http_echo_propfind_method =
        ngx_http_echo_method_name("PROPFIND");
ngx_str_t  ngx_http_echo_proppatch_method =
        ngx_http_echo_method_name("PROPPATCH");


typedef struct ngx_http_echo_subrequest_s {
    ngx_uint_t                  method;
    ngx_str_t                   *method_name;
    ngx_str_t                   *location;
    ngx_str_t                   *query_string;
    ssize_t                     content_length_n;
    ngx_http_request_body_t     *request_body;
} ngx_http_echo_subrequest_t;


static ngx_int_t ngx_http_echo_parse_method_name(ngx_str_t **method_name_ptr);

static ngx_int_t ngx_http_echo_adjust_subrequest(ngx_http_request_t *sr,
        ngx_http_echo_subrequest_t *parsed_sr);

static ngx_int_t ngx_http_echo_parse_subrequest_spec(ngx_http_request_t *r,
        ngx_array_t *computed_args, ngx_http_echo_subrequest_t **parsed_sr_ptr);


ngx_int_t
ngx_http_echo_exec_echo_subrequest_async(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_int_t                       rc;
    ngx_http_echo_subrequest_t      *parsed_sr;
    ngx_http_request_t              *sr; /* subrequest object */
    ngx_str_t                       args;
    ngx_uint_t                      flags = 0;


    dd_enter();

    rc = ngx_http_echo_parse_subrequest_spec(r, computed_args, &parsed_sr);
    if (rc != NGX_OK) {
        return rc;
    }

    dd("location: %.*s",
            (int) parsed_sr->location->len,
            parsed_sr->location->data);

    args.data = NULL;
    args.len = 0;

    if (ngx_http_parse_unsafe_uri(r, parsed_sr->location, &args, &flags)
            != NGX_OK)
    {
        ctx->headers_sent = 1;
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (args.len > 0 && parsed_sr->query_string == NULL) {
        parsed_sr->query_string = &args;
    }

    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (r->header_only || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    rc = ngx_http_subrequest(r, parsed_sr->location, parsed_sr->query_string,
            &sr, NULL, 0);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_http_echo_adjust_subrequest(sr, parsed_sr);

    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_echo_exec_echo_subrequest(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_int_t                            rc;
    ngx_http_request_t                  *sr; /* subrequest object */
    ngx_http_post_subrequest_t          *psr;
    ngx_http_echo_subrequest_t          *parsed_sr;
    ngx_str_t                            args;
    ngx_uint_t                           flags = 0;
    ngx_http_echo_ctx_t                 *sr_ctx;

    dd_enter();

    rc = ngx_http_echo_parse_subrequest_spec(r, computed_args, &parsed_sr);

    if (rc != NGX_OK) {
        return rc;
    }

    args.data = NULL;
    args.len = 0;

    if (ngx_http_parse_unsafe_uri(r, parsed_sr->location, &args, &flags)
            != NGX_OK)
    {
        ctx->headers_sent = 1;

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (args.len > 0 && parsed_sr->query_string == NULL) {
        parsed_sr->query_string = &args;
    }

    rc = ngx_http_echo_send_header_if_needed(r, ctx);

    if (r->header_only || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    sr_ctx = ngx_http_echo_create_ctx(r);

    /* set by ngx_http_echo_create_ctx
     *  sr_ctx->run_post_subrequest = 0
     */

    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));

    if (psr == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    psr->handler = ngx_http_echo_post_subrequest;
    psr->data = sr_ctx;

    rc = ngx_http_subrequest(r, parsed_sr->location, parsed_sr->query_string,
            &sr, psr, 0);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    sr_ctx->sleep.data = sr;

    ngx_http_set_ctx(sr, sr_ctx, ngx_http_echo_module);

    rc = ngx_http_echo_adjust_subrequest(sr, parsed_sr);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_echo_parse_subrequest_spec(ngx_http_request_t *r,
        ngx_array_t *computed_args, ngx_http_echo_subrequest_t **parsed_sr_ptr)
{
    ngx_str_t                   *computed_arg_elts, *arg;
    ngx_str_t                  **to_write = NULL;
    ngx_str_t                   *method_name;
    ngx_str_t                   *body_str = NULL;
    ngx_str_t                   *body_file = NULL;
    ngx_uint_t                   i;
    ngx_flag_t                   expecting_opt;
    ngx_http_request_body_t     *rb = NULL;
    ngx_buf_t                   *b;
    ngx_http_echo_subrequest_t  *parsed_sr;
    ngx_open_file_info_t         of;
    ngx_http_core_loc_conf_t    *clcf;
    size_t                       len;

    *parsed_sr_ptr = ngx_pcalloc(r->pool, sizeof(ngx_http_echo_subrequest_t));
    if (*parsed_sr_ptr == NULL) {
        return NGX_ERROR;
    }

    parsed_sr = *parsed_sr_ptr;

    computed_arg_elts = computed_args->elts;

    method_name = &computed_arg_elts[0];

    parsed_sr->location     = &computed_arg_elts[1];

    if (parsed_sr->location->len == 0) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    expecting_opt = 1;

    for (i = 2; i < computed_args->nelts; i++) {
        arg = &computed_arg_elts[i];

        if (!expecting_opt) {
            if (to_write == NULL) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                        "echo_subrequest_async: to_write should NOT be NULL");
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            *to_write = arg;
            to_write = NULL;

            expecting_opt = 1;

            continue;
        }

        if (arg->len == 2) {
            if (ngx_strncmp("-q", arg->data, arg->len) == 0) {
                to_write = &parsed_sr->query_string;
                expecting_opt = 0;
                continue;
            }

            if (ngx_strncmp("-b", arg->data, arg->len) == 0) {
                to_write = &body_str;
                expecting_opt = 0;
                continue;
            }

            if (ngx_strncmp("-f", arg->data, arg->len) == 0) {
              dd("found option -f");
              to_write = &body_file;
              expecting_opt = 0;
              continue;
            }
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "Unknown option for echo_subrequest_async: %V", arg);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (body_str != NULL && body_str->len) {
        rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));

        if (rb == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        parsed_sr->content_length_n = body_str->len;

        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b->temporary = 1;
        /* b->memory = 1; */
        b->start = b->pos = body_str->data;
        b->end = b->last = body_str->data + body_str->len;

        rb->bufs = ngx_alloc_chain_link(r->pool);
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;
        rb->bufs->next = NULL;

        rb->buf = b;

    } else if (body_file != NULL && body_file->len) {

        dd("body_file defined %.*s", (int) body_file->len, body_file->data);

        body_file->data = ngx_http_echo_rebase_path(r->pool, body_file->data,
                body_file->len, &len);

        if (body_file->data == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        body_file->len = len;

        dd("after rebase, the path becomes %.*s", (int) body_file->len,
                body_file->data);

        rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
        if (rb == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
        ngx_memzero(&of, sizeof(ngx_open_file_info_t));

#if defined(nginx_version) && nginx_version >= 8018
        of.read_ahead = clcf->read_ahead;
#endif

        of.directio = clcf->directio;
        of.valid = clcf->open_file_cache_valid;
        of.min_uses = clcf->open_file_cache_min_uses;
        of.errors = clcf->open_file_cache_errors;
        of.events = clcf->open_file_cache_events;

        if (ngx_open_cached_file(clcf->open_file_cache, body_file, &of, r->pool)
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, of.err,
                    "%s \"%V\" failed",
                    of.failed, body_file);

            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        dd("file content size: %d", (int) of.size);

        parsed_sr->content_length_n = of.size;

        b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
        if (b->file == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b->file_pos = 0;
        b->file_last = of.size;

        b->in_file = b->file_last ? 1: 0;

#if 0
        b->last_buf = (r == r->main) ? 1: 0;
        b->last_in_chain = 1;
#endif

        b->file->fd = of.fd;
        b->file->name = *body_file;
        b->file->log = r->connection->log;
        b->file->directio = of.is_directio;

        rb->bufs = ngx_alloc_chain_link(r->pool);
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;
        rb->bufs->next = NULL;
        rb->buf = b;
    }

    parsed_sr->request_body = rb;

    parsed_sr->method = ngx_http_echo_parse_method_name(&method_name);
    parsed_sr->method_name  = method_name;

    return NGX_OK;
}


static ngx_int_t
ngx_http_echo_adjust_subrequest(ngx_http_request_t *sr,
        ngx_http_echo_subrequest_t *parsed_sr)
{
    ngx_table_elt_t            *h;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_request_t         *r;

    sr->method = parsed_sr->method;
    sr->method_name = *(parsed_sr->method_name);

    r = sr->parent;

    sr->header_in = r->header_in;

    /* XXX work-around a bug in ngx_http_subrequest */
    if (r->headers_in.headers.last == &r->headers_in.headers.part) {
        sr->headers_in.headers.last = &sr->headers_in.headers.part;
    }

    /* we do not inherit the parent request's variables */
    cmcf = ngx_http_get_module_main_conf(sr, ngx_http_core_module);
    sr->variables = ngx_pcalloc(sr->pool, cmcf->variables.nelts
                                        * sizeof(ngx_http_variable_value_t));

    if (sr->variables == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (parsed_sr->content_length_n > 0) {
        sr->headers_in.content_length_n = parsed_sr->content_length_n;
        sr->request_body = parsed_sr->request_body;

        sr->headers_in.content_length = ngx_pcalloc(sr->pool,
                sizeof(ngx_table_elt_t));
        sr->headers_in.content_length->value.data =
            ngx_palloc(sr->pool, NGX_OFF_T_LEN);
        if (sr->headers_in.content_length->value.data == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        sr->headers_in.content_length->value.len = ngx_sprintf(
                sr->headers_in.content_length->value.data, "%O",
                sr->headers_in.content_length_n) -
                sr->headers_in.content_length->value.data;

        if (ngx_list_init(&sr->headers_in.headers, sr->pool, 20,
                    sizeof(ngx_table_elt_t)) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        h = ngx_list_push(&sr->headers_in.headers);
        if (h == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        h->hash = sr->header_hash;

        h->key = ngx_http_echo_content_length_header_key;
        h->value = sr->headers_in.content_length->value;

        h->lowcase_key = ngx_pnalloc(sr->pool, h->key.len);
        if (h->lowcase_key == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_strlow(h->lowcase_key, h->key.data, h->key.len);

        dd("sr content length: %.*s",
                (int) sr->headers_in.content_length->value.len,
                sr->headers_in.content_length->value.data);
    }

    dd("subrequest body: %p", sr->request_body);

    return NGX_OK;
}


static ngx_int_t
ngx_http_echo_parse_method_name(ngx_str_t **method_name_ptr)
{
    const ngx_str_t* method_name = *method_name_ptr;

    switch (method_name->len) {
    case 3:
        if (ngx_http_echo_strcmp_const(method_name->data, "GET") == 0) {
            *method_name_ptr = &ngx_http_echo_get_method;
            return NGX_HTTP_GET;
            break;
        }

        if (ngx_http_echo_strcmp_const(method_name->data, "PUT") == 0) {
            *method_name_ptr = &ngx_http_echo_put_method;
            return NGX_HTTP_PUT;
            break;
        }

        return NGX_HTTP_UNKNOWN;
        break;

    case 4:
        if (ngx_http_echo_strcmp_const(method_name->data, "POST") == 0) {
            *method_name_ptr = &ngx_http_echo_post_method;
            return NGX_HTTP_POST;
            break;
        }
        if (ngx_http_echo_strcmp_const(method_name->data, "HEAD") == 0) {
            *method_name_ptr = &ngx_http_echo_head_method;
            return NGX_HTTP_HEAD;
            break;
        }
        if (ngx_http_echo_strcmp_const(method_name->data, "COPY") == 0) {
            *method_name_ptr = &ngx_http_echo_copy_method;
            return NGX_HTTP_COPY;
            break;
        }
        if (ngx_http_echo_strcmp_const(method_name->data, "MOVE") == 0) {
            *method_name_ptr = &ngx_http_echo_move_method;
            return NGX_HTTP_MOVE;
            break;
        }
        if (ngx_http_echo_strcmp_const(method_name->data, "LOCK") == 0) {
            *method_name_ptr = &ngx_http_echo_lock_method;
            return NGX_HTTP_LOCK;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    case 5:
        if (ngx_http_echo_strcmp_const(method_name->data, "MKCOL") == 0) {
            *method_name_ptr = &ngx_http_echo_mkcol_method;
            return NGX_HTTP_MKCOL;
            break;
        }
        if (ngx_http_echo_strcmp_const(method_name->data, "TRACE") == 0) {
            *method_name_ptr = &ngx_http_echo_trace_method;
            return NGX_HTTP_TRACE;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    case 6:
        if (ngx_http_echo_strcmp_const(method_name->data, "DELETE") == 0) {
            *method_name_ptr = &ngx_http_echo_delete_method;
            return NGX_HTTP_DELETE;
            break;
        }

        if (ngx_http_echo_strcmp_const(method_name->data, "UNLOCK") == 0) {
            *method_name_ptr = &ngx_http_echo_unlock_method;
            return NGX_HTTP_UNLOCK;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    case 7:
        if (ngx_http_echo_strcmp_const(method_name->data, "OPTIONS") == 0) {
            *method_name_ptr = &ngx_http_echo_options_method;
            return NGX_HTTP_OPTIONS;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    case 8:
        if (ngx_http_echo_strcmp_const(method_name->data, "PROPFIND") == 0) {
            *method_name_ptr = &ngx_http_echo_propfind_method;
            return NGX_HTTP_PROPFIND;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    case 9:
        if (ngx_http_echo_strcmp_const(method_name->data, "PROPPATCH") == 0) {
            *method_name_ptr = &ngx_http_echo_proppatch_method;
            return NGX_HTTP_PROPPATCH;
            break;
        }
        return NGX_HTTP_UNKNOWN;
        break;

    default:
        return NGX_HTTP_UNKNOWN;
        break;
    }

    return NGX_HTTP_UNKNOWN;
}


/* XXX extermely evil and not working yet */
ngx_int_t
ngx_http_echo_exec_abort_parent(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx)
{
#if 0
    ngx_http_postponed_request_t    *pr, *ppr;
    ngx_http_request_t              *saved_data = NULL;
    ngx_chain_t                     *out = NULL;
    /* ngx_int_t                       rc; */

    dd("aborting parent...");

    if (r == r->main || r->parent == NULL) {
        return NGX_OK;
    }

    if (r->parent->postponed) {
        dd("Found parent->postponed...");

        saved_data = r->connection->data;
        ppr = NULL;
        for (pr = r->parent->postponed; pr->next; pr = pr->next) {
            if (pr->request == NULL) {
                continue;
            }

            if (pr->request == r) {
                /* r->parent->postponed->next = pr; */
                dd("found the current subrequest");
                out = pr->out;
                continue;
            }

            /* r->connection->data = pr->request; */
            dd("finalizing the subrequest...");
            ngx_http_upstream_create(pr->request);
            pr->request->upstream = NULL;

            if (ppr == NULL) {
                r->parent->postponed = pr->next;
                ppr = pr->next;
            } else {
                ppr->next = pr->next;
                ppr = pr->next;
            }
        }
    }

    r->parent->postponed->next = NULL;

    /*
    r->connection->data = r->parent;
    r->connection->buffered = 0;

    if (out != NULL) {
        dd("trying to send more stuffs for the parent");
        ngx_http_output_filter(r->parent, out);
    }
    */

    /* ngx_http_send_special(r->parent, NGX_HTTP_LAST); */

    if (saved_data) {
        r->connection->data = saved_data;
    }

    dd("terminating the parent request");

    return ngx_http_echo_send_chain_link(r, ctx, NULL /* indicate LAST */);

    /* ngx_http_upstream_create(r); */

    /* ngx_http_finalize_request(r->parent, NGX_ERROR); */
#endif

    return NGX_OK;
}


ngx_int_t
ngx_http_echo_exec_exec(ngx_http_request_t *r,
        ngx_http_echo_ctx_t *ctx, ngx_array_t *computed_args)
{
    ngx_str_t                       *uri;
    ngx_str_t                       *user_args;
    ngx_str_t                        args;
    ngx_uint_t                       flags;
    ngx_str_t                       *computed_arg;

    computed_arg = computed_args->elts;

    uri = &computed_arg[0];

    if (uri->len == 0) {
        return NGX_HTTP_BAD_REQUEST;
    }

    if (computed_args->nelts > 1) {
        user_args = &computed_arg[1];
    } else {
        user_args = NULL;
    }

    args.data = NULL;
    args.len = 0;

    if (ngx_http_parse_unsafe_uri(r, uri, &args, &flags)
            != NGX_OK) {
        ctx->headers_sent = 1;
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (args.len > 0 && user_args == NULL) {
        user_args = &args;
    }

    /*
    rc = ngx_http_echo_send_header_if_needed(r, ctx);
    if (r->header_only || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }
    */

    if (uri->data[0] == '@') {
        if (user_args && user_args->len > 0) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                    "query strings %V ignored when exec'ing named location %V",
                    user_args, uri);

        }

        return ngx_http_named_location(r, uri);
    }

    return ngx_http_internal_redirect(r, uri, user_args);
}

