﻿//
// Project: clibjs
// Created by bajdcc
//

#include "stdafx.h"
#include <cmath>
#include <cfenv>
#include <cstring>
#include <cassert>
#include <sstream>
#include "cjsruntime.h"

#define MAX_SAFE_INTEGER ((int64_t)((1ULL << 53U) - 1))

namespace clib {

    js_value::ref js_value::to_primitive(js_value_new &n, js_value::primitive_t t, int *) {
        return shared_from_this();
    }

    bool js_value::is_primitive() const {
        return true;
    }

    // ----------------------------------

    jsv_number::jsv_number(double n) : number(n) {

    }

    js_runtime_t jsv_number::get_type() {
        return r_number;
    }

    js_value::ref jsv_number::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return shared_from_this();
            case UNARY_NEGATIVE:
                return n.new_number(-number);
            case UNARY_NOT:
                return n.new_boolean(number == 0.0 || std::isnan(number));
            case UNARY_INVERT:
                return n.new_number((double) (int) ~(uint32_t) fix(number));
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("number");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_number::to_bool() const {
        if (std::isnan(number)) return false;
        return number != 0.0;
    }

    void jsv_number::mark(int n) {
        marked = n;
    }

    /* 2 <= base <= 36 */
    static char *i64toa(char *buf_end, int64_t n, unsigned int base) {
        auto q = buf_end;
        uint64_t digit;
        int is_neg;

        is_neg = 0;
        if (n < 0) {
            is_neg = 1;
            n = -n;
        }
        *--q = '\0';
        do {
            digit = ((uint64_t) n) % base;
            n = (uint64_t) n / base;
            if (digit < 10)
                digit += '0';
            else
                digit += 'a' - 10;
            *--q = (char) digit;
        } while (n != 0);
        if (is_neg)
            *--q = '-';
        return q;
    }

    /* buf1 contains the printf result */
    static void js_ecvt1(double d, int n_digits, int *decpt, int *sign, char *buf,
                         int rounding_mode, char *buf1, int buf1_size) {
        if (rounding_mode != FE_TONEAREST)
            fesetround(rounding_mode);
        snprintf(buf1, buf1_size, "%+.*e", n_digits - 1, d);
        if (rounding_mode != FE_TONEAREST)
            fesetround(FE_TONEAREST);
        *sign = (buf1[0] == '-');
        /* mantissa */
        buf[0] = buf1[1];
        if (n_digits > 1)
            memcpy(buf + 1, buf1 + 3, n_digits - 1);
        buf[n_digits] = '\0';
        /* exponent */
        *decpt = atoi(buf1 + n_digits + 2 + (n_digits > 1)) + 1;
    }

/* maximum buffer size for js_dtoa */
#define JS_DTOA_BUF_SIZE 128

/* needed because ecvt usually limits the number of digits to
   17. Return the number of digits. */
    static int js_ecvt(double d, int n_digits, int *decpt, int *sign, char *buf, bool is_fixed) {
        int rounding_mode;
        char buf_tmp[JS_DTOA_BUF_SIZE];

        if (!is_fixed) {
            /* find the minimum amount of digits (XXX: inefficient but simple) */
            auto n_digits_min = 1;
            auto n_digits_max = 17;
            while (n_digits_min < n_digits_max) {
                n_digits = (n_digits_min + n_digits_max) / 2;
                js_ecvt1(d, n_digits, decpt, sign, buf, FE_TONEAREST,
                         buf_tmp, sizeof(buf_tmp));
                if (strtod(buf_tmp, nullptr) == d) {
                    /* no need to keep the trailing zeros */
                    while (n_digits >= 2 && buf[n_digits - 1] == '0')
                        n_digits--;
                    n_digits_max = n_digits;
                } else {
                    n_digits_min = n_digits + 1;
                }
            }
            n_digits = n_digits_max;
            rounding_mode = FE_TONEAREST;
        } else {
            rounding_mode = FE_TONEAREST;
            char buf1[JS_DTOA_BUF_SIZE], buf2[JS_DTOA_BUF_SIZE];
            int decpt1, sign1, decpt2, sign2;
            /* The JS rounding is specified as round to nearest ties away
               from zero (RNDNA), but in printf the "ties" case is not
               specified (for example it is RNDN for glibc, RNDNA for
               Windows), so we must round manually. */
            js_ecvt1(d, n_digits + 1, &decpt1, &sign1, buf1, FE_TONEAREST,
                     buf_tmp, sizeof(buf_tmp));
            /* XXX: could use 2 digits to reduce the average running time */
            if (buf1[n_digits] == '5') {
                js_ecvt1(d, n_digits + 1, &decpt1, &sign1, buf1, FE_DOWNWARD,
                         buf_tmp, sizeof(buf_tmp));
                js_ecvt1(d, n_digits + 1, &decpt2, &sign2, buf2, FE_UPWARD,
                         buf_tmp, sizeof(buf_tmp));
                if (memcmp(buf1, buf2, n_digits + 1) == 0 && decpt1 == decpt2) {
                    /* exact result: round away from zero */
                    if (sign1)
                        rounding_mode = FE_DOWNWARD;
                    else
                        rounding_mode = FE_UPWARD;
                }
            }
        }
        js_ecvt1(d, n_digits, decpt, sign, buf, rounding_mode,
                 buf_tmp, sizeof(buf_tmp));
        return n_digits;
    }

    std::string jsv_number::to_string(js_value_new *n, int hint) const {
        if (hint == 1 && number == 0.0)
            return "0";
        return number_to_string(number);
    }

    double jsv_number::to_number(js_value_new *n) const {
        return number;
    }

    std::string jsv_number::number_to_string(double number) {
        if (std::isnan(number)) {
            return "NaN";
        }
        if (std::isinf(number)) {
            return ((std::signbit(number) == 0) ? "Infinity" : "-Infinity");
        }
        if (number == 0) {
            return ((std::signbit(number) == 0) ? "0" : "-0");
        }
        auto i64 = (int64_t) number;
        char buf1[70];
        if (number != i64 || i64 > MAX_SAFE_INTEGER || i64 < -MAX_SAFE_INTEGER) {
            int sign, decpt, k, n, i, p, n_max;
            n_max = 21;
            /* the number has k digits (k >= 1) */
            k = js_ecvt(number, 0, &decpt, &sign, buf1, false);
            n = decpt; /* d=10^(n-k)*(buf1) i.e. d= < x.yyyy 10^(n-1) */
            char buf[JS_DTOA_BUF_SIZE + 1];
            auto q = buf;
            if (sign)
                *q++ = '-';
            if (n >= 1 && n <= n_max) {
                if (k <= n) {
                    memcpy(q, buf1, k);
                    q += k;
                    for (i = 0; i < (n - k); i++)
                        *q++ = '0';
                    *q = '\0';
                } else {
                    /* k > n */
                    memcpy(q, buf1, n);
                    q += n;
                    *q++ = '.';
                    for (i = 0; i < (k - n); i++)
                        *q++ = buf1[n + i];
                    *q = '\0';
                }
            } else if (n >= -5 && n <= 0) {
                *q++ = '0';
                *q++ = '.';
                for (i = 0; i < -n; i++)
                    *q++ = '0';
                memcpy(q, buf1, k);
                q += k;
                *q = '\0';
            } else {
                /* exponential notation */
                *q++ = buf1[0];
                if (k > 1) {
                    *q++ = '.';
                    for (i = 1; i < k; i++)
                        *q++ = buf1[i];
                }
                *q++ = 'e';
                p = n - 1;
                if (p >= 0)
                    *q++ = '+';
                snprintf(q, JS_DTOA_BUF_SIZE, "%d", p);
            }
            buf[JS_DTOA_BUF_SIZE] = '\0';
            return buf;
        } else {
            return i64toa(buf1 + sizeof(buf1), i64, 10);
        }
    }

    // ----------------------------------

    jsv_string::jsv_string(std::string s) : str(std::move(s)) {

    }

    js_runtime_t jsv_string::get_type() {
        return r_string;
    }

    js_value::ref jsv_string::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE: {
                if (!calc_number)
                    calc();
                if (number_state <= 1)
                    return n.new_number(0.0);
                if (number_state == 2)
                    return n.new_number(number);
                return n.new_number(NAN);
            }
            case UNARY_NEGATIVE: {
                if (!calc_number)
                    calc();
                if (number_state <= 1)
                    return n.new_number(-0.0);
                if (number_state == 2)
                    return n.new_number(-number);
                return n.new_number(NAN);
            }
            case UNARY_NOT:
                return n.new_boolean(str.empty());
            case UNARY_INVERT: {
                if (!calc_number)
                    calc();
                if (number_state == 2)
                    return n.new_number((double) (int) ~(uint32_t) fix(number));
                return n.new_number(-1.0);
            }
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("string");
            default:
                break;
        }
        return nullptr;
    }

    void jsv_string::calc() {
        calc_number = true;
        number_state = to_number(str, number);
    }

    bool jsv_string::to_bool() const {
        return !str.empty();
    }

    void jsv_string::mark(int n) {
        marked = n;
    }

    std::string jsv_string::to_string(js_value_new *n, int hint) const {
        return str;
    }

    double jsv_string::to_number(js_value_new *n) const {
        if (!n)
            return 0;
        auto s = to_string(n, 0);
        double d;
        switch (to_number(s, d)) {
            case 0:
            case 1:
                return 0;
            case 2:
                return d;
            case 3:
                return NAN;
            default:
                break;
        }
        return 0;
    }

    jsv_string::ref jsv_string::clear() {
        if (calc_number) {
            number = 0.0;
            number_state = 0;
            calc_number = false;
        }
        return std::dynamic_pointer_cast<jsv_string>(shared_from_this());
    }

    int jsv_string::to_number(double &d) {
        if (!calc_number) {
            calc();
        }
        if (number_state == 2)
            d = number;
        return number_state;
    }

    int jsv_string::to_number(const std::string &s, double &d) {
        // 0: empty
        // 1: trim -> empty
        // 2: number
        // 3: error
        if (s.empty())
            return 0;
        auto t = js_trim(s);
        if (t == "Infinity") {
            d = INFINITY;
            return 2;
        }
        if (t == "-Infinity") {
            d = -INFINITY;
            return 2;
        }
        std::stringstream ss;
        ss << t;
        if (ss.str().empty())
            return 1;
        ss >> d;
        if (ss.eof() && !ss.fail()) {
            return 2;
        }
        return 3;
    }

    std::string jsv_string::convert(const std::string &_str) {
        std::stringstream ss;
        for (const auto &c : _str) {
            if (c < 0) {
                ss << c;
            } else if (isprint(c)) {
                if (c == '"')
                    ss << "\\\"";
                else if (c == '\'')
                    ss << "\\\'";
                else
                    ss << c;
            } else {
                if (c == '\n')
                    ss << "\\n";
                else {
                    ss << "\\x" << std::hex << (uint32_t) c;
                }
            }
        }
        return ss.str();
    }

    js_value::ref jsv_string::get(js_value_new* n, const std::string& key) const
    {
        static std::regex re_number{ R"(\d+)", std::regex::ECMAScript | std::regex::optimize };
        if (std::regex_match(key, re_number)) {
            std::stringstream ss;
            ss << key;
            size_t idx;
            ss >> idx;
            if (idx < str.length()) {
                return n->new_string(str.substr(idx, 1));
            }
        }
        if (key == "length")
            return n->new_number(str.length());
        return n->new_undefined();
    }

    // ----------------------------------

    std::string jsv_object::_str = "[object Object]";

    js_runtime_t jsv_object::get_type() {
        return r_object;
    }

    js_value::ref jsv_object::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return n.new_number(to_number(&n));
            case UNARY_NEGATIVE:
                return n.new_number(-to_number(&n));
            case UNARY_NOT:
                return n.new_boolean(false);
            case UNARY_INVERT:
                return n.new_number(-1.0);
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("object");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_object::to_bool() const {
        return true;
    }

    void jsv_object::mark(int n) {
        marked = n;
        for (const auto &s : obj) {
            if ((s.second.lock()->marked > 0 ? 1 : 0) != (n > 0 ? 1 : 0))
                s.second.lock()->mark(n);
        }
        for (const auto &s : special) {
            if ((s.second.lock()->marked > 0 ? 1 : 0) != (n > 0 ? 1 : 0))
                s.second.lock()->mark(n);
        }
    }

    js_value::ref jsv_object::gets(const std::string &key) const {
        auto f = obj.find(key);
        if (f != obj.end()) {
            return f->second.lock();
        }
        if (key == "__proto__") {
            auto p = __proto__.lock();
            return p ? p : nullptr;
        }
        auto proto = __proto__.lock();
        if (!proto || proto->get_type() == r_undefined) {
            return nullptr; // type error
        }
        auto p = proto;
        while (p) {
            assert(!p->is_primitive());
            auto o = JS_OBJ(p)->get(key);
            if (o) {
                return o;
            }
            p = p->__proto__.lock();
        }
        return nullptr;
    }

    bool jsv_object::is_primitive() const {
        return false;
    }

    js_value::ref jsv_object::to_primitive(js_value_new &n, js_value::primitive_t t, int *r) {
        assert(r);
        if (t == conv_default)
            t = conv_number;
        switch (t) {
            case conv_number: {
                auto value = gets("valueOf");
                if (value && value->get_type() == r_function) {
                    std::vector<js_value::weak_ref> args;
                    js_value::weak_ref _this = std::const_pointer_cast<js_value>(shared_from_this());
                    auto ret = n.fast_api(JS_FUN(value), _this, args, 0, r);
                    if (ret->is_primitive())
                        return ret;
                }
                value = gets("toString");
                if (value && value->get_type() == r_function) {
                    std::vector<js_value::weak_ref> args;
                    js_value::weak_ref _this = std::const_pointer_cast<js_value>(shared_from_this());
                    auto ret = n.fast_api(JS_FUN(value), _this, args, 0, r);
                    if (ret->is_primitive())
                        return ret;
                }
            }
                break;
            case conv_string: {
                auto value = gets("toString");
                if (value && value->get_type() == r_function) {
                    std::vector<js_value::weak_ref> args;
                    js_value::weak_ref _this = std::const_pointer_cast<js_value>(shared_from_this());
                    auto ret = n.fast_api(JS_FUN(value), _this, args, 0, r);
                    if (ret->is_primitive())
                        return ret;
                }
                value = gets("valueOf");
                if (value && value->get_type() == r_function) {
                    std::vector<js_value::weak_ref> args;
                    js_value::weak_ref _this = std::const_pointer_cast<js_value>(shared_from_this());
                    auto ret = n.fast_api(JS_FUN(value), _this, args, 0, r);
                    if (ret->is_primitive())
                        return ret;
                }
            }
                break;
            default:
                break;
        }
        assert(!"to_primitive error");
        return nullptr;
    }

    std::string jsv_object::to_string(js_value_new *n, int hint) const {
        if (!n) {
            if (attr & at_readonly)
                return "builtin";
            return _str;
        }
        if (!special.empty()) {
            auto f = special.find("PrimitiveValue");
            if (f != special.end()) {
                return f->second.lock()->to_string(n, 0);
            }
        }
        auto type = get("__type__");
        if (!(type && type->attr & at_const)) {
            auto value = gets("toString");
            if (value && value->get_type() == r_function) {
                auto f = JS_FUN(value);
                if (!f->builtin) {
                    std::vector<js_value::weak_ref> args;
                    args.push_back(n->new_number(hint));
                    js_value::weak_ref _this = std::const_pointer_cast<js_value>(shared_from_this());
                    return n->fast_api(f, _this, args, 0)->to_string(n, 0);
                }
            }
        }
        auto v = get("__type__");
        if (v) {
            std::stringstream ss;
            ss << "[object " << v->to_string(n, 0) << "]";
            return ss.str();
        }
        return _str;
    }

    double jsv_object::to_number(js_value_new *n) const {
        auto s = to_string(n, 0);
        auto d = 0.0;
        switch (jsv_string::to_number(s, d)) {
            case 0:
            case 1:
                return 0;
            case 2:
                return d;
            case 3:
                return NAN;
            default:
                break;
        }
        return NAN;
    }

    jsv_object::ref jsv_object::clear() {
        obj.clear();
        keys.clear();
        special.clear();
        return std::dynamic_pointer_cast<jsv_object>(shared_from_this());
    }

    const std::vector<std::string>& jsv_object::get_keys() const
    {
        return keys;
    }

    js_value::ref jsv_object::get(const std::string& key) const
    {
        auto f = obj.find(key);
        if (f == obj.end())
            return nullptr;
        return f->second.lock();
    }

    void jsv_object::add(const std::string& key, const js_value::weak_ref& value)
    {
        if (obj.find(key) == obj.end()) {
            obj.insert({key, value});
            keys.push_back(key);
        }
        else {
            obj[key] = value;
        }
    }

    void jsv_object::remove(const std::string& key)
    {
        auto f = obj.find(key);
        if (f == obj.end()) {
            return;
        }
        obj.erase(f);
        keys.erase(std::remove_if(keys.begin(), keys.end(), [key](const auto& x) {return x == key; }), keys.end());
    }

    void jsv_object::copy_from(const jsv_object::ref& o)
    {
        obj = o->obj;
        keys = o->keys;
    }

    // ----------------------------------

    jsv_function::jsv_function(const js_sym_code_t::ref &c, js_value_new &n) {
        code = std::make_shared<cjs_function_info>(c, n);
    }

    js_runtime_t jsv_function::get_type() {
        return r_function;
    }

    js_value::ref jsv_function::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return n.new_number(NAN);
            case UNARY_NEGATIVE:
                return n.new_number(NAN);
            case UNARY_NOT:
                return n.new_boolean(false);
            case UNARY_INVERT:
                return n.new_number(-1.0);
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("function");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_function::to_bool() const {
        return true;
    }

    void jsv_function::mark(int n) {
        jsv_object::mark(n);
        if (builtin)
            return;
        if (closure.lock()) {
            closure.lock()->mark(n);
        }
    }

    std::string jsv_function::to_string(js_value_new *n, int hint) const {
        if (builtin || attr & at_readonly)
            return name;
        return code ? code->text : "builtin";
    }

    double jsv_function::to_number(js_value_new *n) const {
        return NAN;
    }

    jsv_function::ref jsv_function::clear2() {
        jsv_object::clear();
        code = nullptr;
        closure.reset();
        name.clear();
        return std::dynamic_pointer_cast<jsv_function>(shared_from_this());
    }

    cjs_function::cjs_function(const js_sym_code_t::ref &code, js_value_new &n) {
        reset(code, n);
    }

    cjs_function::cjs_function(cjs_function_info::ref code) {
        reset(std::move(code));
    }

    void cjs_function::store_name(const std::string &n, js_value::weak_ref obj) {
        envs.lock()->add(n, std::move(obj));
    }

    void cjs_function::store_fast(const std::string &n, js_value::weak_ref obj) {
        envs.lock()->add(n, std::move(obj));
    }

    void cjs_function::store_deref(const std::string &n, js_value::weak_ref obj) {
        auto c = closure.lock();
        if (!c)
            return;
        auto var = JS_OBJ(c)->get(n);
        if (!var)
            return;
        if (var->is_primitive())
            return;
        JS_OBJ(var)->add(n, std::move(obj));
    }

    void cjs_function::clear() {
        info = nullptr;
        name = "UNKNOWN";
        pc = 0;
        stack.clear();
        ret_value.reset();
        _this.reset();
        envs.reset();
        closure.reset();
        rests.clear();
        trys.clear();
        exec.clear();
    }

    void cjs_function::reset(const js_sym_code_t::ref &code, js_value_new &n) {
        name = code->debugName;
        info = std::make_shared<cjs_function_info>(code, n);
    }

    void cjs_function::reset(cjs_function_info::ref code) {
        name = code->debugName;
        info = std::move(code);
    }

    cjs_function_info::cjs_function_info(const js_sym_code_t::ref& code, js_value_new& n) {
        arrow = code->arrow;
        debugName = code->debugName;
        simpleName = code->simpleName;
        fullName = code->fullName;
        args = code->args_str;
        std::copy(code->closure_str.begin(), code->closure_str.end(), std::back_inserter(closure));
        codes = code->codes;
        text = code->text;
        rest = code->rest;
        args_num = (int)args.size() - (rest ? 1 : 0);
        const auto& c = code->consts;
        std::copy(c.get_names_data().begin(),
            c.get_names_data().end(),
            std::back_inserter(names));
        std::copy(c.get_globals_data().begin(),
            c.get_globals_data().end(),
            std::back_inserter(globals));
        std::copy(c.get_derefs_data().begin(),
            c.get_derefs_data().end(),
            std::back_inserter(derefs));
        std::copy(c.get_debugs_data().begin(),
            c.get_debugs_data().end(),
            std::back_inserter(debugs));
        consts.resize(c.get_consts_data().size());
        for (size_t i = 0; i < c.get_consts_data().size(); i++) {
            consts[i] = load_const(c, i, n);
            consts[i]->attr |= js_value::at_const;
        }
    }

    js_value::ref cjs_function_info::load_const(const cjs_consts &c, int op, js_value_new &n) {
        auto t = c.get_type(op);
        switch (t) {
            case r_number:
                return n.new_number(*(double *) c.get_data(op));
            case r_string:
                return n.new_string(*(std::string *) c.get_data(op));
            case r_regex: {
                auto re = n.new_regexp();
                re->init(*(std::string*) c.get_data(op), true);
                return re;
            }
            case r_boolean:
                break;
            case r_object:
                break;
            case r_function: {
                auto f = n.new_function();
                auto code = ((js_sym_code_t::weak_ref *) c.get_data(op))->lock();
                f->code = std::make_shared<cjs_function_info>(code, n);
                return f;
            }
            default:
                break;
        }
        assert(!"invalid runtime type");
        return nullptr;
    }

    cjs_function_info::ref cjs_function_info::create_default()
    {
        auto info = std::make_shared<cjs_function_info>();
        info->debugName = info->simpleName = info->fullName = info->text = "<default>";
        return info;
    }

    // ----------------------------------

    std::string jsv_regexp::DEFAULT_VALUE = "(?:)";

    js_runtime_t jsv_regexp::get_type()
    {
        return r_regex;
    }

    std::string jsv_regexp::to_string(js_value_new* n, int hint) const
    {
        return str;
    }

    jsv_regexp::ref jsv_regexp::clear2()
    {
        jsv_object::clear();
        str_origin.clear();
        str.clear();
        error.clear();
        std::swap(re, std::regex());
        return std::dynamic_pointer_cast<jsv_regexp>(shared_from_this());
    }

    void jsv_regexp::init(const std::string& str, bool escape)
    {
        auto s = str.empty() ? DEFAULT_VALUE : str;
        auto f = std::regex::ECMAScript | std::regex::optimize;
        if (escape) {
            assert(s.front() == '/');
            str_origin = s.substr(1);
            while (!this->str_origin.empty()) {
                auto back = this->str_origin.back();
                if (back == '/') {
                    str_origin.pop_back();
                    break;
                }
                switch (back) {
                case 'i': flag |= _i; f |= std::regex::icase; break;
                case 'g': flag |= _g; break;
                case 'm': flag |= _m; break;
                default: {
                    std::stringstream ss;
                    ss << s << ": invalid regex flag: " << back;
                    error = ss.str();
                    return;
                }
                }
                str_origin.pop_back();
            }
            this->str = s;
        }
        else {
            str_origin = s;
            this->str = "/" + s + "/";
        }
        try {
            re = std::regex(str_origin, f);
        }
        catch (const std::exception & e) {
            error = e.what();
        }
    }

    void jsv_regexp::init(const std::string& str, const std::string& flag)
    {
        auto s = str.empty() ? DEFAULT_VALUE : str;
        auto f = std::regex::ECMAScript | std::regex::optimize;
        for (const auto& s : flag) {
            switch (s) {
                case 'i': this->flag |= _i; f |= std::regex::icase; break;
                case 'g': this->flag |= _g; break;
                case 'm': this->flag |= _m; break;
                default: {
                    std::stringstream ss;
                    ss << s << ": invalid regex flag: " << s;
                    error = ss.str();
                    return;
                }
            }
        }
        this->str_origin = s;
        this->str = "/" + s + "/" + flag;
        try {
            re = std::regex(str_origin, f);
        }
        catch (const std::exception & e) {
            error = e.what();
        }
    }

    bool jsv_regexp::test(const std::string& str)
    {
        return std::regex_search(str, re);
    }

    std::string jsv_regexp::replace(const std::string& origin, const std::string& replacer)
    {
        return std::regex_replace(origin, re, replacer,
            flag & _g ? std::regex_constants::match_any : std::regex_constants::match_default);
    }

    bool jsv_regexp::match(const std::string& origin, std::vector<std::tuple<std::string, bool>>& matches)
    {
        if (flag & _g) {
            std::sregex_iterator iter(origin.begin(), origin.end(), re);
            std::sregex_iterator end;
            std::sregex_iterator prev;
            while (iter != end) {
                auto s = iter->prefix().str();
                if (!s.empty()) {
                    matches.push_back({ s, false });
                }
                matches.push_back({ iter->str(), true });
                prev = iter;
                ++iter;
            }
            if (prev != end) {
                auto s = prev->suffix().str();
                if (!s.empty())
                    matches.push_back({ s, false });
            }
            return true;
        }
        else {
            std::smatch sm;
            if (std::regex_search(origin, sm, re)) {
                auto s = sm.prefix().str();
                if (!s.empty()) {
                    matches.push_back({ s, false });
                }
                matches.push_back({ sm.str(), true });
                s = sm.suffix().str();
                if (!s.empty()) {
                    matches.push_back({ s, false });
                }
                return true;
            }
        }
        return false;
    }

    std::string jsv_regexp::replace(const std::string& origin, const std::string& pat, const std::string& replacer)
    {
        size_t start_pos = origin.find(pat);
        if (start_pos == std::string::npos)
            return origin;
        auto dst = origin;
        dst.replace(start_pos, pat.length(), replacer);
        return dst;
    }

    // ----------------------------------

    std::string jsv_boolean::_str_t = "true";

    std::string jsv_boolean::_str_f = "false";

    jsv_boolean::jsv_boolean(bool flag) : b(flag) {

    }

    js_runtime_t jsv_boolean::get_type() {
        return r_boolean;
    }

    js_value::ref jsv_boolean::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return n.new_number(b ? 1.0 : 0.0);
            case UNARY_NEGATIVE:
                return n.new_number(b ? -1.0 : -0.0);
            case UNARY_NOT:
                return n.new_boolean(!b);
            case UNARY_INVERT:
                return n.new_number(b ? -2.0 : -1.0);
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("boolean");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_boolean::to_bool() const {
        return b;
    }

    void jsv_boolean::mark(int n) {
    }

    std::string jsv_boolean::to_string(js_value_new *n, int hint) const {
        return b ? _str_t : _str_f;
    }

    double jsv_boolean::to_number(js_value_new *n) const {
        return b ? 1.0 : 0.0;
    }

    // ----------------------------------

    std::string jsv_null::_str = "null";

    js_runtime_t jsv_null::get_type() {
        return r_null;
    }

    js_value::ref jsv_null::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return n.new_number(0.0);
            case UNARY_NEGATIVE:
                return n.new_number(-0.0);
            case UNARY_NOT:
                return n.new_boolean(true);
            case UNARY_INVERT:
                return n.new_number(-1.0);
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("object");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_null::to_bool() const {
        return false;
    }

    void jsv_null::mark(int n) {
    }

    std::string jsv_null::to_string(js_value_new *n, int hint) const {
        return _str;
    }

    double jsv_null::to_number(js_value_new *n) const {
        return 0;
    }

    // ----------------------------------

    std::string jsv_undefined::_str = "undefined";

    js_runtime_t jsv_undefined::get_type() {
        return r_undefined;
    }

    js_value::ref jsv_undefined::unary_op(js_value_new &n, int code) {
        switch (code) {
            case UNARY_POSITIVE:
                return n.new_number(NAN);
            case UNARY_NEGATIVE:
                return n.new_number(NAN);
            case UNARY_NOT:
                return n.new_boolean(true);
            case UNARY_INVERT:
                return n.new_number(-1.0);
            case UNARY_NEW:
                break;
            case UNARY_DELETE:
                break;
            case UNARY_TYPEOF:
                return n.new_string("undefined");
            default:
                break;
        }
        return nullptr;
    }

    bool jsv_undefined::to_bool() const {
        return false;
    }

    void jsv_undefined::mark(int n) {
    }

    std::string jsv_undefined::to_string(js_value_new *n, int hint) const {
        return _str;
    }

    double jsv_undefined::to_number(js_value_new *n) const {
        return NAN;
    }
}