#pragma once

#include <optional>
#include <sqlpp23/core/basic/table.h>
#include <sqlpp23/core/basic/table_columns.h>
#include <sqlpp23/core/name/create_name_tag.h>
#include <sqlpp23/core/type_traits.h>

namespace test_db {

struct Foo_ {
    struct Id {
        SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(id, Id);
        using data_type = ::sqlpp::integral;
        using has_default = std::true_type;
    };
    struct Name {
        SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(name, Name);
        using data_type = ::sqlpp::text;
        using has_default = std::true_type;
    };
    struct HasFun {
        SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(has_fun, HasFun);
        using data_type = ::sqlpp::boolean;
        using has_default = std::true_type;
    };

    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(foo, foo);

    template <typename T>
    using _table_columns = sqlpp::table_columns<T, Id, Name, HasFun>;
    using _required_insert_columns = sqlpp::detail::type_set<>;
};

using Foo = ::sqlpp::table_t<Foo_>;
inline constexpr Foo foo{};

} // namespace test_db
