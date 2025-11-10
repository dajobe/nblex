/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * filter_engine.c - Field-based and regex filter engine
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* Feature test macros - let nblex_internal.h handle platform-specific setup */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Filter operator types */
typedef enum {
    FILTER_OP_EQ,      /* == */
    FILTER_OP_NE,      /* != */
    FILTER_OP_LT,      /* < */
    FILTER_OP_LE,      /* <= */
    FILTER_OP_GT,      /* >= */
    FILTER_OP_GE,      /* >= */
    FILTER_OP_MATCH,   /* =~ */
    FILTER_OP_NMATCH,  /* !~ */
    FILTER_OP_IN,      /* in */
    FILTER_OP_CONTAINS /* contains */
} filter_op_t;

/* Filter node types */
typedef enum {
    FILTER_NODE_AND,
    FILTER_NODE_OR,
    FILTER_NODE_NOT,
    FILTER_NODE_EXPR
} filter_node_type_t;

/* Filter expression */
typedef struct {
    char* field;
    filter_op_t op;
    json_type value_type;
    union {
        char* string_val;
        int int_val;
        double float_val;
        int bool_val;
    } value;
    pcre2_code* regex_code;
    pcre2_match_data* regex_match_data;
} filter_expr_t;

/* Filter node */
typedef struct filter_node {
    filter_node_type_t type;
    union {
        struct {
            struct filter_node* left;
            struct filter_node* right;
        } binary;
        struct filter_node* unary;
        filter_expr_t* expr;
    } data;
} filter_node_t;

/* Filter context */
typedef struct filter_s {
    filter_node_t* root;
} filter_t;

/* Forward declarations */
static filter_node_t* parse_filter_expr(const char** expr);
static int evaluate_filter_node(const filter_node_t* node, const json_t* event);
static void free_filter_node(filter_node_t* node);

/* Parse filter expression */
static filter_node_t* parse_filter_expr(const char** expr) {
    const char* pos = *expr;

    /* Skip whitespace */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    if (*pos == '(') {
        /* Parenthesized expression */
        pos++;
        filter_node_t* node = parse_filter_expr(&pos);
        if (!node) {
            return NULL;
        }

        while (*pos && isspace(*pos)) {
            pos++;
        }

        if (*pos == ')') {
            pos++;
        }

        *expr = pos;
        return node;
    }

    if (strncmp(pos, "NOT", 3) == 0 || strncmp(pos, "not", 3) == 0) {
        pos += 3;
        filter_node_t* operand = parse_filter_expr(&pos);
        if (!operand) {
            return NULL;
        }

        filter_node_t* node = calloc(1, sizeof(filter_node_t));
        if (!node) {
            free_filter_node(operand);
            return NULL;
        }

        node->type = FILTER_NODE_NOT;
        node->data.unary = operand;

        *expr = pos;
        return node;
    }

    /* Parse field */
    const char* field_start = pos;
    while (*pos && (isalnum(*pos) || *pos == '_' || *pos == '.')) {
        pos++;
    }

    if (pos == field_start) {
        return NULL;
    }

    size_t field_len = pos - field_start;
    char* field = malloc(field_len + 1);
    if (!field) {
        return NULL;
    }
    memcpy(field, field_start, field_len);
    field[field_len] = '\0';

    /* Skip whitespace */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Parse operator */
    filter_op_t op;
    if (strncmp(pos, "==", 2) == 0) {
        op = FILTER_OP_EQ;
        pos += 2;
    } else if (strncmp(pos, "!=", 2) == 0) {
        op = FILTER_OP_NE;
        pos += 2;
    } else if (strncmp(pos, "<=", 2) == 0) {
        op = FILTER_OP_LE;
        pos += 2;
    } else if (strncmp(pos, ">=", 2) == 0) {
        op = FILTER_OP_GE;
        pos += 2;
    } else if (*pos == '<') {
        op = FILTER_OP_LT;
        pos++;
    } else if (*pos == '>') {
        op = FILTER_OP_GT;
        pos++;
    } else if (strncmp(pos, "=~", 2) == 0) {
        op = FILTER_OP_MATCH;
        pos += 2;
    } else if (strncmp(pos, "!~", 2) == 0) {
        op = FILTER_OP_NMATCH;
        pos += 2;
    } else if (strncmp(pos, "in", 2) == 0) {
        op = FILTER_OP_IN;
        pos += 2;
    } else if (strncmp(pos, "contains", 8) == 0) {
        op = FILTER_OP_CONTAINS;
        pos += 8;
    } else {
        free(field);
        return NULL;
    }

    /* Skip whitespace */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Parse value */
    filter_expr_t* expr_data = calloc(1, sizeof(filter_expr_t));
    if (!expr_data) {
        free(field);
        return NULL;
    }

    expr_data->field = field;
    expr_data->op = op;

    if (*pos == '"') {
        /* String value */
        pos++;
        const char* value_start = pos;
        while (*pos && *pos != '"') {
            if (*pos == '\\' && *(pos + 1)) {
                pos += 2;
            } else {
                pos++;
            }
        }

        if (*pos == '"') {
            size_t value_len = pos - value_start;
            expr_data->value_type = JSON_STRING;
            expr_data->value.string_val = malloc(value_len + 1);
            if (expr_data->value.string_val) {
                memcpy(expr_data->value.string_val, value_start, value_len);
                expr_data->value.string_val[value_len] = '\0';
            }
            pos++;
        }
    } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos + 1)))) {
        /* Numeric value */
        char* endptr;
        if (strchr(pos, '.')) {
            double val = strtod(pos, &endptr);
            expr_data->value_type = JSON_REAL;
            expr_data->value.float_val = val;
            pos = endptr;
        } else {
            long val = strtol(pos, &endptr, 10);
            expr_data->value_type = JSON_INTEGER;
            expr_data->value.int_val = val;
            pos = endptr;
        }
    } else if (strncmp(pos, "true", 4) == 0) {
        expr_data->value_type = JSON_TRUE;
        expr_data->value.bool_val = 1;
        pos += 4;
    } else if (strncmp(pos, "false", 5) == 0) {
        expr_data->value_type = JSON_FALSE;
        expr_data->value.bool_val = 0;
        pos += 5;
    } else if (op == FILTER_OP_MATCH || op == FILTER_OP_NMATCH) {
        /* Regex pattern */
        expr_data->value_type = JSON_STRING;
        const char* pattern_start = pos;
        while (*pos && !isspace(*pos)) {
            pos++;
        }

        size_t pattern_len = pos - pattern_start;
        char* pattern = malloc(pattern_len + 1);
        if (pattern) {
            memcpy(pattern, pattern_start, pattern_len);
            pattern[pattern_len] = '\0';

            int error_code;
            PCRE2_SIZE error_offset;
            expr_data->regex_code = pcre2_compile(
                (PCRE2_SPTR)pattern,
                PCRE2_ZERO_TERMINATED,
                PCRE2_UTF | PCRE2_UCP,
                &error_code,
                &error_offset,
                NULL
            );

            if (expr_data->regex_code) {
                expr_data->regex_match_data = pcre2_match_data_create_from_pattern(
                    expr_data->regex_code, NULL
                );
            }

            free(pattern);
        }
    }

    filter_node_t* node = calloc(1, sizeof(filter_node_t));
    if (!node) {
        free(field);
        free(expr_data);
        return NULL;
    }

    node->type = FILTER_NODE_EXPR;
    node->data.expr = expr_data;

    *expr = pos;
    return node;
}

/* Parse full filter expression with AND/OR */
filter_node_t* parse_filter_full(const char* expr) {
    const char* pos = expr;
    filter_node_t* left = parse_filter_expr(&pos);
    if (!left) {
        return NULL;
    }

    /* Check for AND/OR */
    while (*pos) {
        /* Skip whitespace */
        while (*pos && isspace(*pos)) {
            pos++;
        }

        filter_node_type_t op_type;
        size_t op_len;

        if (strncmp(pos, "AND", 3) == 0 || strncmp(pos, "and", 3) == 0) {
            op_type = FILTER_NODE_AND;
            op_len = 3;
        } else if (strncmp(pos, "OR", 2) == 0 || strncmp(pos, "or", 2) == 0) {
            op_type = FILTER_NODE_OR;
            op_len = 2;
        } else {
            break;
        }

        pos += op_len;

        filter_node_t* right = parse_filter_expr(&pos);
        if (!right) {
            free_filter_node(left);
            return NULL;
        }

        filter_node_t* new_node = calloc(1, sizeof(filter_node_t));
        if (!new_node) {
            free_filter_node(left);
            free_filter_node(right);
            return NULL;
        }

        new_node->type = op_type;
        new_node->data.binary.left = left;
        new_node->data.binary.right = right;
        left = new_node;
    }

    return left;
}

/* Evaluate filter expression */
static int evaluate_filter_expr(const filter_expr_t* expr, const json_t* event) {
    if (!expr || !event || !json_is_object(event)) {
        return 0;
    }

    /* Get field value */
    json_t* field_value = json_object_get(event, expr->field);
    if (!field_value) {
        return 0;
    }

    json_type field_type = json_typeof(field_value);

    /* Compare based on operator */
    switch (expr->op) {
        case FILTER_OP_EQ:
            switch (field_type) {
                case JSON_STRING:
                    return expr->value_type == JSON_STRING &&
                           strcmp(json_string_value(field_value), expr->value.string_val) == 0;
                case JSON_INTEGER:
                    return expr->value_type == JSON_INTEGER &&
                           json_integer_value(field_value) == expr->value.int_val;
                case JSON_REAL:
                    return expr->value_type == JSON_REAL &&
                           json_real_value(field_value) == expr->value.float_val;
                case JSON_TRUE:
                case JSON_FALSE:
                    return (expr->value_type == JSON_TRUE && json_is_true(field_value)) ||
                           (expr->value_type == JSON_FALSE && json_is_false(field_value));
                default:
                    return 0;
            }

        case FILTER_OP_NE:
            /* Handle inequality - reuse EQ logic but negate result */
            {
                int equal = 0;
                switch (field_type) {
                    case JSON_STRING:
                        equal = expr->value_type == JSON_STRING &&
                               strcmp(json_string_value(field_value), expr->value.string_val) == 0;
                        break;
                    case JSON_INTEGER:
                        equal = expr->value_type == JSON_INTEGER &&
                               json_integer_value(field_value) == expr->value.int_val;
                        break;
                    case JSON_REAL:
                        equal = expr->value_type == JSON_REAL &&
                               json_real_value(field_value) == expr->value.float_val;
                        break;
                    case JSON_TRUE:
                    case JSON_FALSE:
                        equal = (expr->value_type == JSON_TRUE && json_is_true(field_value)) ||
                               (expr->value_type == JSON_FALSE && json_is_false(field_value));
                        break;
                    default:
                        equal = 0;
                        break;
                }
                return !equal;
            }

        case FILTER_OP_LT:
        case FILTER_OP_LE:
        case FILTER_OP_GT:
        case FILTER_OP_GE:
            if (field_type == JSON_INTEGER && expr->value_type == JSON_INTEGER) {
                long field_val = json_integer_value(field_value);
                long expr_val = expr->value.int_val;
                switch (expr->op) {
                    case FILTER_OP_LT: return field_val < expr_val;
                    case FILTER_OP_LE: return field_val <= expr_val;
                    case FILTER_OP_GT: return field_val > expr_val;
                    case FILTER_OP_GE: return field_val >= expr_val;
                    default: return 0; /* Should not happen in this branch */
                }
            } else if ((field_type == JSON_REAL || field_type == JSON_INTEGER) &&
                      (expr->value_type == JSON_REAL || expr->value_type == JSON_INTEGER)) {
                double field_val = field_type == JSON_REAL ? json_real_value(field_value) : json_integer_value(field_value);
                double expr_val = expr->value_type == JSON_REAL ? expr->value.float_val : expr->value.int_val;
                switch (expr->op) {
                    case FILTER_OP_LT: return field_val < expr_val;
                    case FILTER_OP_LE: return field_val <= expr_val;
                    case FILTER_OP_GT: return field_val > expr_val;
                    case FILTER_OP_GE: return field_val >= expr_val;
                    default: return 0; /* Should not happen in this branch */
                }
            }
            return 0;

        case FILTER_OP_MATCH:
        case FILTER_OP_NMATCH:
            if (field_type == JSON_STRING && expr->regex_code) {
                const char* str = json_string_value(field_value);
                int rc = pcre2_match(
                    expr->regex_code,
                    (PCRE2_SPTR)str,
                    strlen(str),
                    0, 0,
                    expr->regex_match_data,
                    NULL
                );
                int matches = rc >= 0;
                return expr->op == FILTER_OP_MATCH ? matches : !matches;
            }
            return 0;

        default:
            return 0;
    }
}

/* Evaluate filter node */
static int evaluate_filter_node(const filter_node_t* node, const json_t* event) {
    if (!node) {
        return 1; /* Empty filter matches everything */
    }

    switch (node->type) {
        case FILTER_NODE_AND:
            return evaluate_filter_node(node->data.binary.left, event) &&
                   evaluate_filter_node(node->data.binary.right, event);

        case FILTER_NODE_OR:
            return evaluate_filter_node(node->data.binary.left, event) ||
                   evaluate_filter_node(node->data.binary.right, event);

        case FILTER_NODE_NOT:
            return !evaluate_filter_node(node->data.unary, event);

        case FILTER_NODE_EXPR:
            return evaluate_filter_expr(node->data.expr, event);

        default:
            return 0;
    }
}

/* Free filter expression */
static void free_filter_expr(filter_expr_t* expr) {
    if (!expr) {
        return;
    }

    free(expr->field);

    if (expr->value_type == JSON_STRING) {
        free(expr->value.string_val);
    }

    if (expr->regex_code) {
        pcre2_code_free(expr->regex_code);
    }

    if (expr->regex_match_data) {
        pcre2_match_data_free(expr->regex_match_data);
    }

    free(expr);
}

/* Free filter node */
static void free_filter_node(filter_node_t* node) {
    if (!node) {
        return;
    }

    switch (node->type) {
        case FILTER_NODE_AND:
        case FILTER_NODE_OR:
            free_filter_node(node->data.binary.left);
            free_filter_node(node->data.binary.right);
            break;

        case FILTER_NODE_NOT:
            free_filter_node(node->data.unary);
            break;

        case FILTER_NODE_EXPR:
            free_filter_expr(node->data.expr);
            break;
    }

    free(node);
}

/* Create filter from expression */
filter_t* nblex_filter_new(const char* expression) {
    if (!expression) {
        return NULL;
    }

    filter_t* filter = calloc(1, sizeof(filter_t));
    if (!filter) {
        return NULL;
    }

    filter->root = parse_filter_full(expression);

    if (!filter->root) {
        free(filter);
        return NULL;
    }

    return filter;
}

/* Free filter */
void nblex_filter_free(filter_t* filter) {
    if (!filter) {
        return;
    }

    free_filter_node(filter->root);
    free(filter);
}

/* Evaluate filter against event */
int nblex_filter_matches(const filter_t* filter, const nblex_event* event) {
    if (!filter || !event) {
        return 0;
    }

    json_t* data = event->data;
    if (!data) {
        return 0;
    }

    return evaluate_filter_node(filter->root, data);
}
