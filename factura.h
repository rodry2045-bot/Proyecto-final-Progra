#pragma once
#include <string>
#include <vector>

// Tipos de pago
enum class TipoPago { CHEQUE, DEPOSITO, PENDIENTE };

inline std::string tipoPagoStr(TipoPago t) {
    switch (t) {
    case TipoPago::CHEQUE:    return "cheque";
    case TipoPago::DEPOSITO:  return "deposito";
    default:                  return "pendiente";
    }
}
inline TipoPago tipoPagoFromStr(const std::string& s) {
    if (s == "cheque")   return TipoPago::CHEQUE;
    if (s == "deposito") return TipoPago::DEPOSITO;
    return TipoPago::PENDIENTE;
}

// ── Tipos de cliente
enum class TipoCliente { PROVEEDOR, ASOCIACION, MUNICIPALIDAD };

inline std::string tipoClienteStr(TipoCliente t) {
    switch (t) {
    case TipoCliente::PROVEEDOR:    return "proveedor";
    case TipoCliente::ASOCIACION:   return "asociacion";
    case TipoCliente::MUNICIPALIDAD:return "municipalidad";
    default:                        return "proveedor";
    }
}
inline TipoCliente tipoClienteFromStr(const std::string& s) {
    if (s == "asociacion")    return TipoCliente::ASOCIACION;
    if (s == "municipalidad") return TipoCliente::MUNICIPALIDAD;
    return TipoCliente::PROVEEDOR;
}

//Estructura Factura 
struct Factura {
    std::string id;            //ID
    std::string serie;         // Número de serie de la factura
    std::string fechaEmision;  // YYYY-MM-DD
    std::string fechaVencimiento; // YYYY-MM-DD (vacío = sin vencimiento)
    double monto;              // En Quetzales
    TipoPago tipoPago;
    std::string referenciaPago; // Nro. de cheque o depósito
    std::string descripcion;
    bool pagado;
};

// (Proveedor / Asociación / Municipalidad) ─
struct Entidad {
    std::string nit;
    std::string nombre;
    std::string direccion;
    std::string telefono;
    TipoCliente tipo;
    std::vector<Factura> facturas;
};
