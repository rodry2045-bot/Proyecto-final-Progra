/*
 *
 * Control de Facturas (Proveedores + Crédito a Asociaciones/Municipalidades
 *   ./ApiC          → abre http://localhost:8080
 */


#include "httplib.h"
#include "json.hpp"
#include "factura.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <random>
#include <filesystem>
#include <mutex>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std;

// ── Persistencia ────────────────────────────────────────────────────────────
static const std::string DATA_FILE = "datos.json";
static std::mutex g_mutex;

// ── Colecciones en memoria ───────────────────────────────────────────────────
static std::vector<Entidad> g_entidades;

// ── Utilidades ───────────────────────────────────────────────────────────────
std::string generarId() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i) id += hex[dist(rng)];
    return id;
}

// ── JSON ↔ Structs ───────────────────────────────────────────────────────────
json facturaToJson(const Factura& f) {
    return {
        {"id",               f.id},
        {"serie",            f.serie},
        {"fechaEmision",     f.fechaEmision},
        {"fechaVencimiento", f.fechaVencimiento},
        {"monto",            f.monto},
        {"tipoPago",         tipoPagoStr(f.tipoPago)},
        {"referenciaPago",   f.referenciaPago},
        {"descripcion",      f.descripcion},
        {"pagado",           f.pagado}
    };
}

Factura facturaFromJson(const json& j) {
    Factura f;
    f.id = j.value("id", generarId());
    f.serie = j.value("serie", "");
    f.fechaEmision = j.value("fechaEmision", "");
    f.fechaVencimiento = j.value("fechaVencimiento", "");
    f.monto = j.value("monto", 0.0);
    f.tipoPago = tipoPagoFromStr(j.value("tipoPago", "pendiente"));
    f.referenciaPago = j.value("referenciaPago", "");
    f.descripcion = j.value("descripcion", "");
    f.pagado = j.value("pagado", false);
    return f;
}

json entidadToJson(const Entidad& e) {
    json arr = json::array();
    for (auto& f : e.facturas) arr.push_back(facturaToJson(f));
    return {
        {"nit",       e.nit},
        {"nombre",    e.nombre},
        {"direccion", e.direccion},
        {"telefono",  e.telefono},
        {"tipo",      tipoClienteStr(e.tipo)},
        {"facturas",  arr}
    };
}

Entidad entidadFromJson(const json& j) {
    Entidad e;
    e.nit = j.value("nit", "");
    e.nombre = j.value("nombre", "");
    e.direccion = j.value("direccion", "");
    e.telefono = j.value("telefono", "");
    e.tipo = tipoClienteFromStr(j.value("tipo", "proveedor"));
    if (j.contains("facturas")) {
        for (auto& fj : j["facturas"]) e.facturas.push_back(facturaFromJson(fj));
    }
    return e;
}

// ── Guardar / Cargar ─────────────────────────────────────────────────────────
void guardarDatos() {
    json arr = json::array();
    for (auto& e : g_entidades) arr.push_back(entidadToJson(e));
    std::ofstream f(DATA_FILE);
    f << arr.dump(2);
}

void cargarDatos() {
    if (!fs::exists(DATA_FILE)) return;
    std::ifstream f(DATA_FILE);
    if (!f.is_open()) return;
    try {
        json arr = json::parse(f);
        for (auto& ej : arr) g_entidades.push_back(entidadFromJson(ej));
        std::cout << "[INFO] Datos cargados: " << g_entidades.size() << " entidad(es).\n";
    }
    catch (...) {
        std::cerr << "[WARN] No se pudo parsear " << DATA_FILE << "\n";
    }
}

// ── Buscar entidad por NIT ───────────────────────────────────────────────────
Entidad* encontrarEntidad(const std::string& nit) {
    for (auto& e : g_entidades)
        if (e.nit == nit) return &e;
    return nullptr;
}

// ── CORS helper ──────────────────────────────────────────────────────────────
void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

void jsonResp(httplib::Response& res, int code, const json& body) {
    setCORS(res);
    res.status = code;
    res.set_content(body.dump(), "application/json");
}

// ── Leer archivo HTML ────────────────────────────────────────────────────────
std::string leerArchivo(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════
int main() {
    cargarDatos();

    httplib::Server svr;

    .
    svr.set_payload_max_length(1024 * 1024); // 1 MB

    // ── Servir index.html ────────────────────────────────────────────────
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::string html = leerArchivo("index.html");
        if (html.empty()) {
            res.status = 404;
            res.set_content("index.html no encontrado en la carpeta actual.", "text/plain");
            return;
        }
        setCORS(res);
        res.set_content(html, "text/html; charset=utf-8");
        });

    // OPTIONS (preflight CORS)
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
        });

    // ══ ENTIDADES

    // entidades,tipo=proveedor|asociacion|municipalidad
    svr.Get("/entidades", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        std::string filtro = req.get_param_value("tipo");
        json arr = json::array();
        for (auto& e : g_entidades) {
            if (filtro.empty() || tipoClienteStr(e.tipo) == filtro)
                arr.push_back(entidadToJson(e));
        }
        jsonResp(res, 200, arr);
        });

    // GET /entidades/:nit
    svr.Get(R"(/entidades/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        jsonResp(res, 200, entidadToJson(*e));
        });

    // POST /entidades — crear entidad
    svr.Post("/entidades", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        try {
            json body = json::parse(req.body);
            std::string nit = body.value("nit", "");
            if (nit.empty()) { jsonResp(res, 400, { {"error", "NIT requerido"} }); return; }
            if (encontrarEntidad(nit)) { jsonResp(res, 409, { {"error", "Entidad ya existe"} }); return; }
            Entidad e = entidadFromJson(body);
            g_entidades.push_back(e);
            guardarDatos();
            jsonResp(res, 201, entidadToJson(e));
        }
        catch (...) { jsonResp(res, 400, { {"error", "JSON inválido"} }); }
        });

    // PUT /entidades/:nit — actualizar datos
    svr.Put(R"(/entidades/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        try {
            json body = json::parse(req.body);
            if (body.contains("nombre"))    e->nombre = body["nombre"];
            if (body.contains("direccion")) e->direccion = body["direccion"];
            if (body.contains("telefono"))  e->telefono = body["telefono"];
            if (body.contains("tipo"))      e->tipo = tipoClienteFromStr(body["tipo"]);
            guardarDatos();
            jsonResp(res, 200, entidadToJson(*e));
        }
        catch (...) { jsonResp(res, 400, { {"error", "JSON inválido"} }); }
        });

    // DELETE /entidades/:nit
   
    svr.Delete(R"(/entidades/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        std::string nit = req.matches[1].str(); // conversión explícita
        auto it = std::remove_if(g_entidades.begin(), g_entidades.end(),
            [&nit](const Entidad& e) { return e.nit == nit; });
        if (it == g_entidades.end()) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        g_entidades.erase(it, g_entidades.end());
        guardarDatos();
        jsonResp(res, 200, { {"ok", true} });
        });

    // ══ FACTURAS ═══════════════════════════════════════════════════════════

    // GET /entidades/:nit/facturas
    svr.Get(R"(/entidades/([^/]+)/facturas)", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        json arr = json::array();
        for (auto& f : e->facturas) arr.push_back(facturaToJson(f));
        jsonResp(res, 200, arr);
        });

    // POST /entidades/:nit/facturas
    svr.Post(R"(/entidades/([^/]+)/facturas)", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        try {
            json body = json::parse(req.body);
            Factura f = facturaFromJson(body);
            if (f.id.empty()) f.id = generarId();
            e->facturas.push_back(f);
            guardarDatos();
            jsonResp(res, 201, facturaToJson(f));
        }
        catch (...) { jsonResp(res, 400, { {"error", "JSON inválido"} }); }
        });

    // PUT /entidades/:nit/facturas/:id
    svr.Put(R"(/entidades/([^/]+)/facturas/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        std::string fid = req.matches[2].str(); // conversión explícita (mismo bug #3)
        auto it = std::find_if(e->facturas.begin(), e->facturas.end(),
            [&fid](const Factura& f) { return f.id == fid; });
        if (it == e->facturas.end()) { jsonResp(res, 404, { {"error", "Factura no encontrada"} }); return; }
        try {
            json body = json::parse(req.body);
            if (body.contains("serie"))            it->serie = body["serie"];
            if (body.contains("fechaEmision"))     it->fechaEmision = body["fechaEmision"];
            if (body.contains("fechaVencimiento")) it->fechaVencimiento = body["fechaVencimiento"];
            if (body.contains("monto"))            it->monto = body["monto"];
            if (body.contains("tipoPago"))         it->tipoPago = tipoPagoFromStr(body["tipoPago"]);
            if (body.contains("referenciaPago"))   it->referenciaPago = body["referenciaPago"];
            if (body.contains("descripcion"))      it->descripcion = body["descripcion"];
            if (body.contains("pagado"))           it->pagado = body["pagado"];
            guardarDatos();
            jsonResp(res, 200, facturaToJson(*it));
        }
        catch (...) { jsonResp(res, 400, { {"error", "JSON inválido"} }); }
        });

    // DELETE /entidades/:nit/facturas/:id
    svr.Delete(R"(/entidades/([^/]+)/facturas/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto* e = encontrarEntidad(req.matches[1]);
        if (!e) { jsonResp(res, 404, { {"error", "Entidad no encontrada"} }); return; }
        std::string fid = req.matches[2].str(); // conversión explícita (bug #3)
        auto it = std::remove_if(e->facturas.begin(), e->facturas.end(),
            [&fid](const Factura& f) { return f.id == fid; });
        if (it == e->facturas.end()) { jsonResp(res, 404, { {"error", "Factura no encontrada"} }); return; }
        e->facturas.erase(it, e->facturas.end());
        guardarDatos();
        jsonResp(res, 200, { {"ok", true} });
        });

    // ══ RESUMEN ════════════════════════════════════════════════════════════
    // GET /resumen?tipo=...   → totales vencidas / pendientes / pagadas
    svr.Get("/resumen", [](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mutex);
        std::string filtro = req.get_param_value("tipo");

        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&tt));
        std::string hoy(buf);

        double totalVencidas = 0, totalPendientes = 0, totalPagadas = 0;
        int cntVencidas = 0, cntPendientes = 0, cntPagadas = 0;

        for (auto& e : g_entidades) {
            if (!filtro.empty() && tipoClienteStr(e.tipo) != filtro) continue;
            for (auto& f : e.facturas) {
                if (f.pagado) {
                    totalPagadas += f.monto; cntPagadas++;
                }
                else if (!f.fechaVencimiento.empty() && f.fechaVencimiento < hoy) {
                    totalVencidas += f.monto; cntVencidas++;
                }
                else {
                    totalPendientes += f.monto; cntPendientes++;
                }
            }
        }

        jsonResp(res, 200, {
            {"vencidas",   {{"total", totalVencidas},   {"count", cntVencidas}}},
            {"pendientes", {{"total", totalPendientes}, {"count", cntPendientes}}},
            {"pagadas",    {{"total", totalPagadas},    {"count", cntPagadas}}}
            });
        });

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║   Sistema de Facturacion - ApiC          ║\n";
    std::cout << "║   http://localhost:8080                  ║\n";
    std::cout << "║   Ctrl+C para detener                    ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    svr.listen("0.0.0.0", 8080);
    return 0;
}
