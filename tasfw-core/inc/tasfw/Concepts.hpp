#pragma once
#include <concepts>
#include <tuple>
#include <type_traits>

#ifndef CONCEPTS_H
#define CONCEPTS_H

template <template <typename...> class Template, typename... Args>
void derived_from_specialization_impl(const Template<Args...>&);

template <class T, template <typename...> class Template>
concept derived_from_specialization_of = requires(const T& t)
{
	derived_from_specialization_impl<Template>(t);
};

// T constructible from the elements of TTuple, a std::tuple<Ts...> possibly cv-qualified (a
// container's element), the way ExecuteFromTuple builds a script from a parameter tuple. A
// class template rather than a requires-expression over std::apply: apply's deduced return
// type makes a mismatch a hard error inside the library instead of a constraint that does not
// hold (docs/compilers.md, ROADMAP 3.10).
template <class T, class TTuple>
struct constructible_from_tuple_impl : std::false_type {};

template <class T, typename... Ts>
struct constructible_from_tuple_impl<T, std::tuple<Ts...>> : std::bool_constant<std::constructible_from<T, Ts...>> {};

template <class T, typename TTuple>
concept constructible_from_tuple = constructible_from_tuple_impl<T, std::remove_cv_t<TTuple>>::value;

#endif
