/*
 * Copyright 2017-2018 Baidu Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "openrasp_hook.h"

int include_handler(ZEND_OPCODE_HANDLER_ARGS);
int eval_handler(ZEND_OPCODE_HANDLER_ARGS);
int include_or_eval_handler(ZEND_OPCODE_HANDLER_ARGS)
{
    if (OPENRASP_INCLUDE_OR_EVAL_TYPE(execute_data->opline) == ZEND_EVAL)
    {
        return eval_handler(ZEND_OPCODE_HANDLER_ARGS_PASSTHRU);
    }
    else
    {
        return include_handler(ZEND_OPCODE_HANDLER_ARGS_PASSTHRU);
    }
}
int eval_handler(ZEND_OPCODE_HANDLER_ARGS)
{
    zend_op *opline = execute_data->opline;
    if (!openrasp_check_type_ignored(ZEND_STRL("webshell_eval") TSRMLS_CC) &&
        OPENRASP_OP1_TYPE(opline) == IS_VAR &&
        openrasp_zval_in_request(OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr TSRMLS_CC))
    {
        zval *attack_params;
        MAKE_STD_ZVAL(attack_params);
        array_init(attack_params);
        add_assoc_zval(attack_params, "eval", OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr);
        Z_ADDREF_P(OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr);
        zval *plugin_message = NULL;
        MAKE_STD_ZVAL(plugin_message);
        ZVAL_STRING(plugin_message, _("WebShell activity - Detected China Chopper webshell (eval method)"), 1);
        openrasp_buildin_php_risk_handle(1, "webshell_eval", 100, attack_params, plugin_message TSRMLS_CC);
    }
    return ZEND_USER_OPCODE_DISPATCH;
}
int include_handler(ZEND_OPCODE_HANDLER_ARGS)
{
    zend_op *opline = execute_data->opline;
    zval *op1 = nullptr;
    switch (OPENRASP_OP1_TYPE(opline))
    {
    case IS_TMP_VAR:
    {
        op1 = &OPENRASP_T(OPENRASP_OP1_VAR(opline)).tmp_var;
        break;
    }
    case IS_VAR:
    {
        // whether the parameter is the user input data
        if (!openrasp_check_type_ignored(ZEND_STRL("webshell_include") TSRMLS_CC) 
        && openrasp_zval_in_request(OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr TSRMLS_CC))
        {
            zval *attack_params;
            MAKE_STD_ZVAL(attack_params);
            array_init(attack_params);
            add_assoc_zval(attack_params, "url", OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr);
            Z_ADDREF_P(OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr);
            zval *plugin_message = NULL;
            MAKE_STD_ZVAL(plugin_message);
            ZVAL_STRING(plugin_message, _("File inclusion - Detected include/require vulnerability or webshell based on the same mechanism"), 1);
            openrasp_buildin_php_risk_handle(1, "webshell_include", 100, attack_params, plugin_message TSRMLS_CC);
        }
        op1 = OPENRASP_T(OPENRASP_OP1_VAR(opline)).var.ptr;
        break;
    }
    case IS_CV:
    {
        zval **t = OPENRASP_CV(OPENRASP_OP1_VAR(opline));
        if (t && *t)
        {
            op1 = *t;
        }
        else if (EG(active_symbol_table))
        {
            zend_compiled_variable *cv = &EG(active_op_array)->vars[OPENRASP_OP1_VAR(opline)];
            if (zend_hash_quick_find(EG(active_symbol_table), cv->name, cv->name_len + 1, cv->hash_value, (void **)&t) == SUCCESS)
            {
                op1 = *t;
            }
        }
        break;
    }
    case IS_CONST:
    {
        op1 = OPENRASP_OP1_CONSTANT_PTR(opline);
        break;
    }
    default:
    {
        return ZEND_USER_OPCODE_DISPATCH;
    }
    }
    if (openrasp_check_type_ignored(ZEND_STRL("include") TSRMLS_CC))
    {
        return ZEND_USER_OPCODE_DISPATCH;
    }
    zval *path;
    MAKE_STD_ZVAL(path);
    MAKE_COPY_ZVAL(&op1, path);
    convert_to_string(path);
    char *real_path = nullptr;
    const char *scheme_end = nullptr;
    if (Z_STRVAL_P(path) && (scheme_end = fetch_url_scheme(Z_STRVAL_P(path))) != nullptr || (strlen(Z_STRVAL_P(path)) < 4 || 
    (strcmp(Z_STRVAL_P(path) + Z_STRLEN_P(path) - 4, ".php") && strcmp(Z_STRVAL_P(path) + Z_STRLEN_P(path) - 4, ".inc"))))
    {
        real_path = openrasp_real_path(Z_STRVAL_P(path), Z_STRLEN_P(path), 1, READING TSRMLS_CC);
    }

    if (!real_path)
    {
        zval_ptr_dtor(&path);
    }
    else
    {
        zval **doc_root;
        bool send_to_plugin = false;
        if (scheme_end || strstr(Z_STRVAL_P(path), "../") != nullptr)
        {
            send_to_plugin = true;
        }
        if (!PG(http_globals)[TRACK_VARS_SERVER] && !zend_is_auto_global(ZEND_STRL("_SERVER") TSRMLS_CC))
        {
            send_to_plugin = true;
        }
        if (Z_TYPE_P(PG(http_globals)[TRACK_VARS_SERVER]) == IS_ARRAY)
        {
            if (zend_hash_find(Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_SERVER]), ZEND_STRS("DOCUMENT_ROOT"), (void **)&doc_root) == FAILURE)
            {
                send_to_plugin = true;
            }
            else
            {   
                assert(Z_TYPE_PP(doc_root) == IS_STRING);
                if (0 == strncmp(real_path, Z_STRVAL_PP(doc_root), Z_STRLEN_PP(doc_root)))
                {
                    send_to_plugin = false;
                }
                else
                {
                    send_to_plugin = true;
                }  
            }            
        }
        if (send_to_plugin)
        {
            zval *params;
            MAKE_STD_ZVAL(params);
            array_init(params);
            add_assoc_zval(params, "path", path);
            add_assoc_zval(params, "url", path);
            Z_ADDREF_P(path);
            add_assoc_string(params, "realpath", real_path, 0);
            char *function = nullptr;
            switch (OPENRASP_INCLUDE_OR_EVAL_TYPE(execute_data->opline))
            {
            case ZEND_INCLUDE:
                function = "include";
                break;
            case ZEND_INCLUDE_ONCE:
                function = "include_once";
                break;
            case ZEND_REQUIRE:
                function = "require";
                break;
            case ZEND_REQUIRE_ONCE:
                function = "require_once";
                break;
            default:
                function = "";
                break;
            }
            add_assoc_string(params, "function", function, 1);
            check("include", params TSRMLS_CC);
        }
        else
        {
            //skip
            efree(real_path);
            zval_ptr_dtor(&path);
        }
    }
    return ZEND_USER_OPCODE_DISPATCH;
}