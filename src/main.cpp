#include <sqlite3.h>

#if LOCALLENS_USE_COMMONCRYPTO
#include <CommonCrypto/CommonDigest.h>
#elif LOCALLENS_USE_OPENSSL
#include <openssl/sha.h>
#else
#error "LocalLens needs CommonCrypto on macOS or OpenSSL on Linux."
#endif

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

struct Db {
  sqlite3 *raw = nullptr;
  explicit Db(const fs::path &path) {
    fs::create_directories(path.parent_path());
    if (sqlite3_open(path.string().c_str(), &raw) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
  }
  ~Db() { sqlite3_close(raw); }
  void exec(const std::string &sql) {
    char *err = nullptr;
    if (sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
      std::string msg = err ? err : "sqlite error";
      sqlite3_free(err);
      throw std::runtime_error(msg);
    }
  }
};

static void bind_value(sqlite3_stmt *s, int i, const std::string &v) { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); }
static void bind_value(sqlite3_stmt *s, int i, const char *v) { sqlite3_bind_text(s, i, v, -1, SQLITE_TRANSIENT); }
static void bind_value(sqlite3_stmt *s, int i, std::int64_t v) { sqlite3_bind_int64(s, i, v); }

template <class... Args> void run(Db &db, const std::string &sql, Args... args) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db.raw, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  int index = 1;
  (bind_value(stmt, index++, args), ...);
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    std::string msg = sqlite3_errmsg(db.raw);
    sqlite3_finalize(stmt);
    throw std::runtime_error(msg);
  }
  sqlite3_finalize(stmt);
}

static std::optional<std::string> one(Db &db, const std::string &sql, const std::string &arg) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db.raw, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  bind_value(stmt, 1, arg);
  std::optional<std::string> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) out = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  sqlite3_finalize(stmt);
  return out;
}

static std::string hex(const unsigned char *data, size_t n) {
  std::ostringstream out;
  for (size_t i = 0; i < n; ++i) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  return out.str();
}

static std::string sha256(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  std::array<char, 64 * 1024> buf{};
#if LOCALLENS_USE_COMMONCRYPTO
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  while (in) {
    in.read(buf.data(), buf.size());
    if (in.gcount()) CC_SHA256_Update(&ctx, buf.data(), static_cast<CC_LONG>(in.gcount()));
  }
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  return hex(digest, sizeof digest);
#else
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  while (in) {
    in.read(buf.data(), buf.size());
    if (in.gcount()) SHA256_Update(&ctx, buf.data(), static_cast<size_t>(in.gcount()));
  }
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256_Final(digest, &ctx);
  return hex(digest, sizeof digest);
#endif
}

static void copy_and_fsync(const fs::path &from, const fs::path &to) {
  fs::create_directories(to.parent_path());
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!in || !out) throw std::runtime_error("copy open failed for " + from.string());
  out << in.rdbuf();
  if (in.bad() || !out) throw std::runtime_error("copy failed for " + from.string());
  out.flush();
}

static std::string iso_time(fs::file_time_type t) {
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  std::time_t c = std::chrono::system_clock::to_time_t(sctp);
  std::tm tm{};
  gmtime_r(&c, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

static std::string layout(const fs::path &root) {
  if (fs::exists(root / "DCIM" / "Movie")) return "70mai-a810";
  if (fs::exists(root / "Tapo")) return "tapo-d210";
  if (fs::exists(root / "DCIM" / "RECORD")) return "reolink-argus-pt";
  return "unknown";
}

static bool media_ext(const fs::path &p) {
  auto e = p.extension().string();
  for (auto &c : e) c = static_cast<char>(std::tolower(c));
  return e == ".mp4" || e == ".mov" || e == ".avi" || e == ".jpg" || e == ".jpeg";
}

static std::vector<fs::path> scan(const fs::path &root) {
  std::vector<fs::path> out;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), end; it != end; ++it) {
    if (it->is_symlink()) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file() && media_ext(it->path())) out.push_back(it->path());
  }
  return out;
}

static void migrate(Db &db) {
  db.exec(R"SQL(
    PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS device(id INTEGER PRIMARY KEY, layout TEXT UNIQUE NOT NULL);
    CREATE TABLE IF NOT EXISTS import_session(id INTEGER PRIMARY KEY, root TEXT NOT NULL, layout TEXT NOT NULL, started_at TEXT DEFAULT CURRENT_TIMESTAMP, finished_at TEXT);
    CREATE TABLE IF NOT EXISTS media_object(id INTEGER PRIMARY KEY, sha256 TEXT UNIQUE NOT NULL, size INTEGER NOT NULL, stored_path TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS media_source(id INTEGER PRIMARY KEY, media_id INTEGER NOT NULL, session_id INTEGER NOT NULL, source_path TEXT NOT NULL, source_mtime TEXT NOT NULL, confidence TEXT NOT NULL, UNIQUE(session_id, source_path));
    CREATE TABLE IF NOT EXISTS error(id INTEGER PRIMARY KEY, session_id INTEGER, source_path TEXT, message TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS audit_event(id INTEGER PRIMARY KEY, event TEXT NOT NULL, detail TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
  )SQL");
}

static int import_root(const fs::path &root, const fs::path &store, Db &db) {
  if (!fs::exists(root) || !fs::is_directory(root)) throw std::runtime_error("root is not a directory");
  const auto name = layout(root);
  run(db, "INSERT OR IGNORE INTO device(layout) VALUES(?)", name);
  run(db, "INSERT INTO import_session(root, layout) VALUES(?, ?)", fs::weakly_canonical(root).string(), name);
  const auto session = sqlite3_last_insert_rowid(db.raw);
  fs::path staging = store / "staging" / std::to_string(session);
  fs::path media = store / "media";
  fs::path quarantine = store / "quarantine" / std::to_string(session);
  int imported = 0;
  for (const auto &src : scan(root)) {
    const auto rel = fs::relative(src, root);
    try {
      const auto tmp = staging / rel;
      copy_and_fsync(src, tmp);
      const auto src_hash = sha256(src);
      const auto tmp_hash = sha256(tmp);
      if (src_hash != tmp_hash) throw std::runtime_error("hash mismatch");
      const auto size = static_cast<std::int64_t>(fs::file_size(src));
      auto final_rel = fs::path(src_hash.substr(0, 2)) / src_hash.substr(2, 2) / (src_hash + src.extension().string());
      auto final = media / final_rel;
      if (!fs::exists(final)) {
        fs::create_directories(final.parent_path());
        fs::rename(tmp, final);
      } else {
        fs::remove(tmp);
      }
      run(db, "INSERT OR IGNORE INTO media_object(sha256,size,stored_path) VALUES(?,?,?)", src_hash, size, final.string());
      auto media_id = one(db, "SELECT id FROM media_object WHERE sha256=?", src_hash).value();
      run(db, "INSERT OR IGNORE INTO media_source(media_id,session_id,source_path,source_mtime,confidence) VALUES(?,?,?,?,?)",
          media_id, static_cast<std::int64_t>(session), rel.string(), iso_time(fs::last_write_time(src)), "filesystem");
      ++imported;
      std::cout << "{\"event\":\"imported\",\"source\":\"" << rel.string() << "\",\"sha256\":\"" << src_hash << "\"}\n";
    } catch (const std::exception &e) {
      fs::create_directories(quarantine);
      run(db, "INSERT INTO error(session_id,source_path,message) VALUES(?,?,?)", static_cast<std::int64_t>(session), rel.string(), e.what());
      std::cerr << "{\"event\":\"quarantined\",\"source\":\"" << rel.string() << "\",\"message\":\"" << e.what() << "\"}\n";
    }
  }
  run(db, "UPDATE import_session SET finished_at=CURRENT_TIMESTAMP WHERE id=?", static_cast<std::int64_t>(session));
  run(db, "INSERT INTO audit_event(event,detail) VALUES(?,?)", "safe_eject_ready", root.string());
  std::cout << "{\"event\":\"safe_eject_ready\",\"root\":\"" << root.string() << "\",\"layout\":\"" << name << "\",\"imported\":" << imported << "}\n";
  return imported;
}

static void status(Db &db) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = R"SQL(
    SELECT
      (SELECT count(*) FROM import_session),
      (SELECT count(*) FROM media_object),
      (SELECT count(*) FROM media_source),
      (SELECT count(*) FROM error)
  )SQL";
  if (sqlite3_prepare_v2(db.raw, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  sqlite3_step(stmt);
  std::cout << "{\"sessions\":" << sqlite3_column_int64(stmt, 0)
            << ",\"objects\":" << sqlite3_column_int64(stmt, 1)
            << ",\"sources\":" << sqlite3_column_int64(stmt, 2)
            << ",\"errors\":" << sqlite3_column_int64(stmt, 3) << "}\n";
  sqlite3_finalize(stmt);
}

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      std::cerr << "usage: locallens <db> import <mount-root> <store-root> | locallens <db> status\n";
      return 2;
    }
    Db db(argv[1]);
    migrate(db);
    std::string cmd = argv[2];
    if (cmd == "import" && argc == 5) return import_root(argv[3], argv[4], db) >= 0 ? 0 : 1;
    if (cmd == "status" && argc == 3) {
      status(db);
      return 0;
    }
    std::cerr << "invalid command\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "{\"event\":\"fatal\",\"message\":\"" << e.what() << "\"}\n";
    return 1;
  }
}
