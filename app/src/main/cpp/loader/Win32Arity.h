#pragma once

// Aridad de las funciones nativas que ve el invitado, para la convención de llamada de 32 bits.
//
// En x86-32 los argumentos van en la pila y, con stdcall (casi toda la API de Windows), los saca la
// función llamada: el puente necesita saber cuántos bytes ocupa cada llamada. La fuente fiable es la
// propia firma C++ del envoltorio: hpFn(&win_X) la registra (slots de 4 bytes; int64_t/double ocupan
// 2) y devuelve el mismo puntero que antes reinterpret_cast<void*>(&win_X).

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hyperplay::w32arity {

struct Arity {
    uint8_t slots = 0;       // palabras de 32 bits en la pila
    uint8_t args = 0;        // argumentos C++
    uint16_t wideMask = 0;   // bit i: el argumento i ocupa 2 slots (int64_t/double)
    bool variadic = false;   // "...": cdecl (la limpia el llamador)
};

void record(const void* fn, const Arity& a);
// false si la función no se registró con hpFn (aridad desconocida).
bool lookup(const void* fn, Arity& out);

template <class T>
constexpr bool isWide() {
    using U = std::remove_cv_t<T>;
    return std::is_same_v<U, int64_t> || std::is_same_v<U, double> || std::is_same_v<U, long long>;
}

template <class... A>
constexpr Arity arityOf(bool variadic) {
    Arity a{};
    a.args = static_cast<uint8_t>(sizeof...(A));
    a.variadic = variadic;
    constexpr bool wide[sizeof...(A) + 1] = {isWide<A>()..., false};
    for (size_t i = 0; i < sizeof...(A); ++i) {
        if (wide[i]) a.wideMask = static_cast<uint16_t>(a.wideMask | (1u << i));
        a.slots = static_cast<uint8_t>(a.slots + (wide[i] ? 2 : 1));
    }
    return a;
}

template <class R, class... A>
void* hpFn(R (*f)(A...)) {
    record(reinterpret_cast<const void*>(f), arityOf<A...>(false));
    return reinterpret_cast<void*>(f);
}

template <class R, class... A>
void* hpFn(R (*f)(A..., ...)) {
    record(reinterpret_cast<const void*>(f), arityOf<A...>(true));
    return reinterpret_cast<void*>(f);
}

// Datos (no funciones): sin aridad.
template <class T, std::enable_if_t<!std::is_function_v<T>, int> = 0>
void* hpFn(T* p) {
    return const_cast<void*>(reinterpret_cast<const void*>(p));
}

}  // namespace hyperplay::w32arity
