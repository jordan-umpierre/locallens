#include <sqlite3.h>

#if LOCALLENS_USE_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#else
#error "LocalLens needs OpenSSL."
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;

#ifndef LOCALLENS_VERSION
#define LOCALLENS_VERSION "dev"
#endif

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
  void exec_ignore_duplicate_column(const std::string &sql) {
    char *err = nullptr;
    if (sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, &err) == SQLITE_OK) return;
    std::string msg = err ? err : "sqlite error";
    sqlite3_free(err);
    if (msg.find("duplicate column name") == std::string::npos) throw std::runtime_error(msg);
  }
};

static void bind_value(sqlite3_stmt *s, int i, const std::string &v) { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); }
static void bind_value(sqlite3_stmt *s, int i, const char *v) { sqlite3_bind_text(s, i, v, -1, SQLITE_TRANSIENT); }
static void bind_value(sqlite3_stmt *s, int i, std::int64_t v) { sqlite3_bind_int64(s, i, v); }

static std::string text_col(sqlite3_stmt *s, int i) {
  const auto *v = sqlite3_column_text(s, i);
  return v ? reinterpret_cast<const char *>(v) : "";
}

static std::string json(std::string_view s) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\b') out << "\\b";
    else if (c == '\f') out << "\\f";
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
    else out << c;
  }
  out << '"';
  return out.str();
}

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

static std::optional<std::string> one(Db &db, const std::string &sql, std::int64_t arg) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db.raw, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  bind_value(stmt, 1, arg);
  std::optional<std::string> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) out = text_col(stmt, 0);
  sqlite3_finalize(stmt);
  return out;
}

static std::string hex(const unsigned char *data, size_t n) {
  std::ostringstream out;
  for (size_t i = 0; i < n; ++i) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  return out.str();
}

static const std::string &trace_id() {
  static const std::string id = [] {
    if (const char *provided = std::getenv("LOCALLENS_TRACE_ID"); provided && *provided) return std::string(provided);
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1) throw std::runtime_error("trace id generation failed");
    return hex(bytes.data(), bytes.size());
  }();
  return id;
}

static std::string sha256_bytes(const unsigned char *data, size_t size) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  if (EVP_Digest(data, size, digest.data(), &digest_size, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("sha256 failed");
  }
  return hex(digest.data(), digest_size);
}

static std::string sha256(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) throw std::runtime_error("sha256 context failed");
  std::array<char, 64 * 1024> buf{};
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  const auto ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
  bool updated = ok;
  while (updated && in) {
    in.read(buf.data(), buf.size());
    if (in.gcount()) updated = EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(in.gcount())) == 1;
  }
  const bool finished = updated && !in.bad() && EVP_DigestFinal_ex(ctx, digest.data(), &digest_size) == 1;
  EVP_MD_CTX_free(ctx);
  if (!finished) throw std::runtime_error("sha256 failed");
  return hex(digest.data(), digest_size);
}

static void copy_and_fsync(const fs::path &from, const fs::path &to) {
  fs::create_directories(to.parent_path());
  const int source = open(from.c_str(), O_RDONLY);
  const int target = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (source < 0 || target < 0) {
    if (source >= 0) close(source);
    if (target >= 0) close(target);
    throw std::runtime_error("copy open failed");
  }
  std::array<char, 64 * 1024> buf{};
  bool ok = true;
  while (ok) {
    const auto got = read(source, buf.data(), buf.size());
    if (got == 0) break;
    if (got < 0) {
      ok = false;
      break;
    }
    for (ssize_t offset = 0; offset < got;) {
      const auto written = write(target, buf.data() + offset, static_cast<size_t>(got - offset));
      if (written <= 0) {
        ok = false;
        break;
      }
      offset += written;
    }
  }
  ok = ok && fsync(target) == 0;
  close(source);
  close(target);
  if (!ok) throw std::runtime_error("copy failed");
}

static void fsync_directory(const fs::path &path) {
  const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0 || fsync(fd) != 0) {
    if (fd >= 0) close(fd);
    throw std::runtime_error("directory sync failed");
  }
  close(fd);
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

static std::string now_token() {
  return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

static std::string media_type(const fs::path &p) {
  auto e = p.extension().string();
  for (auto &c : e) c = static_cast<char>(std::tolower(c));
  return e == ".jpg" || e == ".jpeg" ? "image" : "video";
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
  std::sort(out.begin(), out.end());
  return out;
}

static void migrate(Db &db) {
  db.exec(R"SQL(
    PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS device(id INTEGER PRIMARY KEY, layout TEXT UNIQUE NOT NULL);
    CREATE TABLE IF NOT EXISTS import_session(id INTEGER PRIMARY KEY, root TEXT NOT NULL, layout TEXT NOT NULL, started_at TEXT DEFAULT CURRENT_TIMESTAMP, finished_at TEXT);
    CREATE TABLE IF NOT EXISTS media_object(id INTEGER PRIMARY KEY, sha256 TEXT UNIQUE NOT NULL, size INTEGER NOT NULL, stored_path TEXT NOT NULL, media_type TEXT NOT NULL DEFAULT 'video');
    CREATE TABLE IF NOT EXISTS media_source(id INTEGER PRIMARY KEY, media_id INTEGER NOT NULL, session_id INTEGER NOT NULL, source_path TEXT NOT NULL, source_mtime TEXT NOT NULL, confidence TEXT NOT NULL, timezone TEXT NOT NULL DEFAULT 'UTC', UNIQUE(session_id, source_path));
    CREATE TABLE IF NOT EXISTS user_annotation(id INTEGER PRIMARY KEY, media_id INTEGER NOT NULL, corrected_time TEXT, tags TEXT NOT NULL DEFAULT '', notes TEXT NOT NULL DEFAULT '', actor TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS derived_asset(id INTEGER PRIMARY KEY, media_id INTEGER NOT NULL, kind TEXT NOT NULL, path TEXT NOT NULL, size INTEGER NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS playback_grant(token TEXT PRIMARY KEY, media_id INTEGER NOT NULL, expires_at TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS backup_set(id INTEGER PRIMARY KEY, manifest_id TEXT UNIQUE NOT NULL, remote_root TEXT NOT NULL, object_count INTEGER NOT NULL, plain_bytes INTEGER NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS backup_object(id INTEGER PRIMARY KEY, backup_set_id INTEGER NOT NULL, logical_path TEXT NOT NULL, plain_sha256 TEXT NOT NULL, cipher_sha256 TEXT NOT NULL, plain_size INTEGER NOT NULL, cipher_size INTEGER NOT NULL, nonce_hex TEXT NOT NULL, tag_hex TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS verification_run(id INTEGER PRIMARY KEY, manifest_id TEXT NOT NULL, checked_objects INTEGER NOT NULL, result TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS restore_run(id INTEGER PRIMARY KEY, manifest_id TEXT NOT NULL, restored_objects INTEGER NOT NULL, result TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS error(id INTEGER PRIMARY KEY, session_id INTEGER, source_path TEXT, message TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS audit_event(id INTEGER PRIMARY KEY, event TEXT NOT NULL, detail TEXT NOT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP);
    CREATE VIRTUAL TABLE IF NOT EXISTS search_document USING fts5(media_id UNINDEXED, source_path, layout, sha256, tags, notes);
  )SQL");
  db.exec_ignore_duplicate_column("ALTER TABLE media_object ADD COLUMN media_type TEXT NOT NULL DEFAULT 'video'");
  db.exec_ignore_duplicate_column("ALTER TABLE media_source ADD COLUMN timezone TEXT NOT NULL DEFAULT 'UTC'");
  db.exec(R"SQL(
    INSERT INTO search_document(rowid, media_id, source_path, layout, sha256, tags, notes)
      SELECT m.id, m.id, group_concat(s.source_path, ' '), group_concat(DISTINCT i.layout), m.sha256,
             coalesce((SELECT group_concat(tags, ' ') FROM user_annotation a WHERE a.media_id=m.id), ''),
             coalesce((SELECT group_concat(notes, ' ') FROM user_annotation a WHERE a.media_id=m.id), '')
      FROM media_object m
      JOIN media_source s ON s.media_id=m.id
      JOIN import_session i ON i.id=s.session_id
      WHERE NOT EXISTS (SELECT 1 FROM search_document d WHERE d.media_id=m.id)
      GROUP BY m.id;
  )SQL");
  db.exec("PRAGMA user_version=2");
}

static std::vector<unsigned char> from_hex(const std::string &s) {
  if (s.size() % 2) throw std::runtime_error("invalid hex");
  std::vector<unsigned char> out;
  for (size_t i = 0; i < s.size(); i += 2) out.push_back(static_cast<unsigned char>(std::stoul(s.substr(i, 2), nullptr, 16)));
  return out;
}

static std::vector<unsigned char> read_key(const fs::path &path, bool create) {
  if (!fs::exists(path) && create) {
    std::array<unsigned char, 32> key{};
    if (RAND_bytes(key.data(), key.size()) != 1) throw std::runtime_error("key generation failed");
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << hex(key.data(), key.size()) << "\n";
    out.close();
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
  }
  if (!fs::exists(path)) throw std::runtime_error("recovery key missing");
  std::ifstream in(path);
  std::string key_hex;
  in >> key_hex;
  auto key = from_hex(key_hex);
  if (key.size() != 32) throw std::runtime_error("recovery key must be 32 bytes hex");
  return key;
}

static std::vector<unsigned char> read_all(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void write_all(const fs::path &path, const std::vector<unsigned char> &bytes) {
  fs::create_directories(path.parent_path());
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) throw std::runtime_error("cannot write object");
  bool ok = true;
  for (size_t offset = 0; ok && offset < bytes.size();) {
    const auto written = write(fd, bytes.data() + offset, bytes.size() - offset);
    if (written <= 0) ok = false;
    else offset += static_cast<size_t>(written);
  }
  ok = ok && fsync(fd) == 0;
  close(fd);
  if (!ok) throw std::runtime_error("cannot write object");
  fsync_directory(path.parent_path());
}

static std::vector<unsigned char> aes_gcm(bool encrypt, const std::vector<unsigned char> &key, const std::vector<unsigned char> &nonce,
                                          const std::vector<unsigned char> &input, std::vector<unsigned char> &tag) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("crypto context failed");
  auto free_ctx = [&] { EVP_CIPHER_CTX_free(ctx); };
  if (EVP_CipherInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr, encrypt ? 1 : 0) != 1) {
    free_ctx();
    throw std::runtime_error("crypto init failed");
  }
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 ||
      EVP_CipherInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data(), encrypt ? 1 : 0) != 1) {
    free_ctx();
    throw std::runtime_error("crypto key setup failed");
  }
  if (!encrypt && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), tag.data()) != 1) {
    free_ctx();
    throw std::runtime_error("crypto tag setup failed");
  }
  std::vector<unsigned char> out(input.size() + 16);
  int written = 0;
  if (EVP_CipherUpdate(ctx, out.data(), &written, input.data(), static_cast<int>(input.size())) != 1) {
    free_ctx();
    throw std::runtime_error("crypto update failed");
  }
  int final_written = 0;
  if (EVP_CipherFinal_ex(ctx, out.data() + written, &final_written) != 1) {
    free_ctx();
    throw std::runtime_error("authentication failed");
  }
  out.resize(static_cast<size_t>(written + final_written));
  if (encrypt) {
    tag.resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
      free_ctx();
      throw std::runtime_error("crypto tag failed");
    }
  }
  free_ctx();
  return out;
}

struct ManifestRow {
  std::string logical_path;
  std::string plain_sha;
  std::string cipher_sha;
  std::int64_t plain_size = 0;
  std::int64_t cipher_size = 0;
  std::string nonce_hex;
  std::string tag_hex;
};

static std::string field(const std::string &line, const std::string &name) {
  const auto key = "\"" + name + "\":";
  auto start = line.find(key);
  if (start == std::string::npos) throw std::runtime_error("manifest field missing");
  start += key.size();
  if (start >= line.size() || line[start++] != '"') throw std::runtime_error("manifest field is not a string");
  std::string value;
  while (start < line.size()) {
    const char c = line[start++];
    if (c == '"') return value;
    if (static_cast<unsigned char>(c) < 0x20) throw std::runtime_error("manifest contains an unescaped control character");
    if (c != '\\') { value += c; continue; }
    if (start >= line.size()) throw std::runtime_error("manifest escape is incomplete");
    const char escaped = line[start++];
    if (escaped == '"' || escaped == '\\' || escaped == '/') value += escaped;
    else if (escaped == 'b') value += '\b';
    else if (escaped == 'f') value += '\f';
    else if (escaped == 'n') value += '\n';
    else if (escaped == 'r') value += '\r';
    else if (escaped == 't') value += '\t';
    else if (escaped == 'u') {
      if (start + 4 > line.size()) throw std::runtime_error("manifest unicode escape is incomplete");
      const auto encoded = line.substr(start, 4);
      if (!std::all_of(encoded.begin(), encoded.end(), [](unsigned char digit) { return std::isxdigit(digit); }))
        throw std::runtime_error("manifest unicode escape is invalid");
      const auto code = std::stoul(encoded, nullptr, 16);
      if (code > 0x7f) throw std::runtime_error("manifest unicode escape is unsupported");
      value += static_cast<char>(code);
      start += 4;
    } else throw std::runtime_error("manifest escape is invalid");
  }
  throw std::runtime_error("manifest string is unterminated");
}

static bool fixed_hex(const std::string &value, size_t size) {
  return value.size() == size && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c); });
}

static std::int64_t nonnegative_integer(const std::string &value) {
  std::uint64_t parsed = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
      parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    throw std::runtime_error("manifest integer is invalid");
  return static_cast<std::int64_t>(parsed);
}

static std::vector<ManifestRow> manifest_rows(const std::string &contents) {
  std::istringstream in(contents);
  std::vector<ManifestRow> rows;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("\"type\":\"object\"") == std::string::npos) continue;
    ManifestRow r{field(line, "logical_path"), field(line, "plain_sha256"), field(line, "cipher_sha256"), nonnegative_integer(field(line, "plain_size")),
                  nonnegative_integer(field(line, "cipher_size")), field(line, "nonce_hex"), field(line, "tag_hex")};
    if (r.logical_path.empty() || r.plain_size < 0 || r.cipher_size < 0 || !fixed_hex(r.plain_sha, 64) || !fixed_hex(r.cipher_sha, 64) ||
        !fixed_hex(r.nonce_hex, 24) || !fixed_hex(r.tag_hex, 32)) throw std::runtime_error("manifest object is invalid");
    rows.push_back(r);
  }
  return rows;
}

static std::vector<ManifestRow> read_manifest(const fs::path &remote, const std::string &manifest_id,
                                               const std::vector<unsigned char> &key) {
  const auto sealed = read_all(remote / "manifests" / (manifest_id + ".bin"));
  if (sealed.size() < 28) throw std::runtime_error("invalid manifest");
  std::vector<unsigned char> nonce(sealed.begin(), sealed.begin() + 12);
  std::vector<unsigned char> tag(sealed.begin() + 12, sealed.begin() + 28);
  std::vector<unsigned char> cipher(sealed.begin() + 28, sealed.end());
  const auto plain = aes_gcm(false, key, nonce, cipher, tag);
  const auto rows = manifest_rows(std::string(plain.begin(), plain.end()));
  if (rows.empty()) throw std::runtime_error("empty manifest");
  return rows;
}

static fs::path object_path(const fs::path &remote, const ManifestRow &row) {
  return remote / "objects" / (row.cipher_sha + ".bin");
}

static void backup(Db &db, const fs::path &store, const fs::path &remote, const fs::path &key_path) {
  const auto key = read_key(key_path, true);
  const auto manifest_id = now_token();
  const auto manifest = remote / "manifests" / (manifest_id + ".bin");
  db.exec("PRAGMA wal_checkpoint(FULL)");
  fs::create_directories(remote / "objects");
  fs::create_directories(manifest.parent_path());
  std::ostringstream mf;
  mf << "{\"type\":\"manifest\",\"version\":2,\"id\":" << json(manifest_id) << ",\"cipher\":\"AES-256-GCM\",\"key_version\":\"offline-v1\"}\n";
  std::vector<fs::path> files{fs::path("catalog.db")};
  for (fs::recursive_directory_iterator it(store / "media"), end; it != end; ++it) {
    if (it->is_regular_file()) files.push_back(fs::relative(it->path(), store));
  }
  std::int64_t count = 0, plain_bytes = 0;
  run(db, "BEGIN");
  run(db, "INSERT INTO backup_set(manifest_id,remote_root,object_count,plain_bytes) VALUES(?,?,0,0)", manifest_id, remote.string());
  const auto set_id = sqlite3_last_insert_rowid(db.raw);
  for (const auto &rel : files) {
    const auto src = rel == fs::path("catalog.db") ? fs::path(sqlite3_db_filename(db.raw, "main")) : store / rel;
    if (!fs::exists(src) || fs::is_directory(src)) continue;
    std::vector<unsigned char> nonce(12), tag;
    if (RAND_bytes(nonce.data(), nonce.size()) != 1) throw std::runtime_error("nonce generation failed");
    const auto plain = read_all(src);
    auto cipher = aes_gcm(true, key, nonce, plain, tag);
    const auto plain_sha = sha256(src);
    const auto cipher_sha = sha256_bytes(cipher.data(), cipher.size());
    const auto cipher_path = remote / "objects" / (cipher_sha + ".bin");
    write_all(cipher_path, cipher);
    mf << "{\"type\":\"object\",\"logical_path\":" << json(rel.string()) << ",\"plain_sha256\":" << json(plain_sha)
       << ",\"cipher_sha256\":" << json(cipher_sha) << ",\"plain_size\":\"" << plain.size() << "\",\"cipher_size\":\"" << cipher.size()
       << "\",\"nonce_hex\":" << json(hex(nonce.data(), nonce.size())) << ",\"tag_hex\":" << json(hex(tag.data(), tag.size())) << "}\n";
    run(db, "INSERT INTO backup_object(backup_set_id,logical_path,plain_sha256,cipher_sha256,plain_size,cipher_size,nonce_hex,tag_hex) VALUES(?,?,?,?,?,?,?,?)",
        static_cast<std::int64_t>(set_id), rel.string(), plain_sha, cipher_sha, static_cast<std::int64_t>(plain.size()), static_cast<std::int64_t>(cipher.size()),
        hex(nonce.data(), nonce.size()), hex(tag.data(), tag.size()));
    ++count;
    plain_bytes += static_cast<std::int64_t>(plain.size());
  }
  run(db, "UPDATE backup_set SET object_count=?, plain_bytes=? WHERE id=?", count, plain_bytes, static_cast<std::int64_t>(set_id));
  const auto manifest_text = mf.str();
  std::vector<unsigned char> manifest_plain(manifest_text.begin(), manifest_text.end());
  std::vector<unsigned char> nonce(12), tag;
  if (RAND_bytes(nonce.data(), nonce.size()) != 1) throw std::runtime_error("nonce generation failed");
  const auto manifest_cipher = aes_gcm(true, key, nonce, manifest_plain, tag);
  std::vector<unsigned char> sealed;
  sealed.insert(sealed.end(), nonce.begin(), nonce.end());
  sealed.insert(sealed.end(), tag.begin(), tag.end());
  sealed.insert(sealed.end(), manifest_cipher.begin(), manifest_cipher.end());
  write_all(manifest, sealed);
  run(db, "COMMIT");
  std::cout << "{\"event\":\"backup_complete\",\"trace_id\":" << json(trace_id()) << ",\"manifest_id\":" << json(manifest_id) << ",\"objects\":" << count << ",\"plain_bytes\":" << plain_bytes << "}\n";
}

static void verify_backup(Db &db, const fs::path &remote, const fs::path &key_path, const std::string &manifest_id) {
  const auto key = read_key(key_path, false);
  const auto rows = read_manifest(remote, manifest_id, key);
  for (const auto &r : rows) {
    const auto cipher_path = object_path(remote, r);
    if (sha256(cipher_path) != r.cipher_sha) throw std::runtime_error("remote object hash mismatch");
    auto tag = from_hex(r.tag_hex);
    auto plain = aes_gcm(false, key, from_hex(r.nonce_hex), read_all(cipher_path), tag);
    fs::path tmp = remote / "verify.tmp";
    write_all(tmp, plain);
    if (sha256(tmp) != r.plain_sha) throw std::runtime_error("plaintext hash mismatch");
    fs::remove(tmp);
  }
  run(db, "INSERT INTO verification_run(manifest_id,checked_objects,result) VALUES(?,?,?)", manifest_id, static_cast<std::int64_t>(rows.size()), "ok");
  std::cout << "{\"event\":\"backup_verified\",\"trace_id\":" << json(trace_id()) << ",\"manifest_id\":" << json(manifest_id) << ",\"objects\":" << rows.size() << "}\n";
}

static void restore_backup(Db &db, const fs::path &remote, const fs::path &target, const fs::path &key_path, const std::string &manifest_id) {
  if (fs::exists(target) && !fs::is_empty(target)) throw std::runtime_error("restore target must be empty");
  const auto key = read_key(key_path, false);
  const auto rows = read_manifest(remote, manifest_id, key);
  for (const auto &r : rows) {
    const fs::path logical = r.logical_path;
    if (logical.empty() || logical.is_absolute() || std::find(logical.begin(), logical.end(), "..") != logical.end()) {
      throw std::runtime_error("unsafe manifest path");
    }
    auto tag = from_hex(r.tag_hex);
    auto plain = aes_gcm(false, key, from_hex(r.nonce_hex), read_all(object_path(remote, r)), tag);
    write_all(target / r.logical_path, plain);
    if (sha256(target / r.logical_path) != r.plain_sha) throw std::runtime_error("restored hash mismatch");
  }
  run(db, "INSERT INTO restore_run(manifest_id,restored_objects,result) VALUES(?,?,?)", manifest_id, static_cast<std::int64_t>(rows.size()), "ok");
  std::cout << "{\"event\":\"restore_complete\",\"trace_id\":" << json(trace_id()) << ",\"manifest_id\":" << json(manifest_id) << ",\"objects\":" << rows.size() << "}\n";
}

static void refresh_search(Db &db, std::int64_t media_id) {
  run(db, "DELETE FROM search_document WHERE media_id=?", media_id);
  run(db, R"SQL(
    INSERT INTO search_document(rowid, media_id, source_path, layout, sha256, tags, notes)
    SELECT m.id, m.id, group_concat(s.source_path, ' '), group_concat(DISTINCT i.layout), m.sha256,
           coalesce((SELECT group_concat(tags, ' ') FROM user_annotation a WHERE a.media_id=m.id), ''),
           coalesce((SELECT group_concat(notes, ' ') FROM user_annotation a WHERE a.media_id=m.id), '')
    FROM media_object m
    JOIN media_source s ON s.media_id=m.id
    JOIN import_session i ON i.id=s.session_id
    WHERE m.id=?
    GROUP BY m.id
  )SQL", media_id);
}

static int import_root(const fs::path &root, const fs::path &store, Db &db) {
  if (!fs::exists(root) || !fs::is_directory(root)) throw std::runtime_error("root is not a directory");
  const auto name = layout(root);
  if (name == "unknown") throw std::runtime_error("unsupported layout");
  run(db, "INSERT OR IGNORE INTO device(layout) VALUES(?)", name);
  run(db, "INSERT INTO import_session(root, layout) VALUES(?, ?)", fs::weakly_canonical(root).string(), name);
  const auto session = sqlite3_last_insert_rowid(db.raw);
  fs::path staging = store / "staging" / std::to_string(session);
  fs::path media = store / "media";
  fs::path quarantine = store / "quarantine" / std::to_string(session);
  int imported = 0, failed = 0;
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
        fsync_directory(final.parent_path());
      } else {
        fs::remove(tmp);
      }
      run(db, "INSERT OR IGNORE INTO media_object(sha256,size,stored_path,media_type) VALUES(?,?,?,?)", src_hash, size, final.string(), media_type(src));
      auto media_id = one(db, "SELECT id FROM media_object WHERE sha256=?", src_hash).value();
      run(db, "INSERT OR IGNORE INTO media_source(media_id,session_id,source_path,source_mtime,confidence,timezone) VALUES(?,?,?,?,?,?)",
          media_id, static_cast<std::int64_t>(session), rel.string(), iso_time(fs::last_write_time(src)), "filesystem", "UTC");
      refresh_search(db, std::stoll(media_id));
      ++imported;
      std::cout << "{\"event\":\"imported\",\"trace_id\":" << json(trace_id()) << ",\"source\":" << json(rel.string()) << ",\"sha256\":" << json(src_hash) << "}\n";
    } catch (const std::exception &e) {
      fs::create_directories(quarantine);
      run(db, "INSERT INTO error(session_id,source_path,message) VALUES(?,?,?)", static_cast<std::int64_t>(session), rel.string(), e.what());
      std::cerr << "{\"event\":\"quarantined\",\"trace_id\":" << json(trace_id()) << ",\"source\":" << json(rel.string()) << ",\"code\":\"import_failed\"}\n";
      ++failed;
    }
  }
  run(db, "UPDATE import_session SET finished_at=CURRENT_TIMESTAMP WHERE id=?", static_cast<std::int64_t>(session));
  const auto event = failed == 0 ? "safe_eject_ready" : "import_review_required";
  run(db, "INSERT INTO audit_event(event,detail) VALUES(?,?)", event, root.string());
  std::cout << "{\"event\":" << json(event) << ",\"trace_id\":" << json(trace_id()) << ",\"root\":" << json(root.string()) << ",\"layout\":" << json(name) << ",\"imported\":" << imported << ",\"failed\":" << failed << "}\n";
  return failed == 0 ? imported : -1;
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

static void health(Db &db, const fs::path &store) {
  const auto capacity = fs::space(store);
  sqlite3_stmt *stmt = nullptr;
  const char *sql = R"SQL(
    SELECT
      (SELECT count(*) FROM error),
      coalesce((SELECT manifest_id FROM backup_set ORDER BY id DESC LIMIT 1), ''),
      coalesce((SELECT result FROM verification_run ORDER BY id DESC LIMIT 1), ''),
      (SELECT user_version FROM pragma_user_version)
  )SQL";
  if (sqlite3_prepare_v2(db.raw, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  sqlite3_step(stmt);
  const auto errors = sqlite3_column_int64(stmt, 0);
  const auto backup = text_col(stmt, 1);
  const auto verification = text_col(stmt, 2);
  std::cout << "{\"status\":" << json(errors == 0 ? "ok" : "review-required")
            << ",\"schema_version\":" << sqlite3_column_int64(stmt, 3)
            << ",\"storage_capacity_bytes\":" << capacity.capacity
            << ",\"storage_available_bytes\":" << capacity.available
            << ",\"import_errors\":" << errors
            << ",\"latest_backup\":" << json(backup)
            << ",\"latest_verification\":" << json(verification) << "}\n";
  sqlite3_finalize(stmt);
}

static std::string fts_query(std::string_view q) {
  std::ostringstream out;
  bool first = true;
  std::string term;
  auto flush = [&] {
    if (term.empty()) return;
    if (!first) out << " AND ";
    out << '"' << term << '"';
    first = false;
    term.clear();
  };
  for (char c : q) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') term += c;
    else flush();
  }
  flush();
  return out.str();
}

static void timeline(Db &db) {
  sqlite3_stmt *stmt = nullptr;
  const char *sql = R"SQL(
    SELECT m.id, m.sha256, m.media_type, m.size, min(s.source_mtime), s.confidence, s.timezone,
           coalesce((SELECT corrected_time FROM user_annotation a WHERE a.media_id=m.id AND corrected_time IS NOT NULL ORDER BY id DESC LIMIT 1), ''),
           group_concat(DISTINCT i.layout),
           (SELECT count(*) FROM media_source ds WHERE ds.media_id=m.id)
    FROM media_object m
    JOIN media_source s ON s.media_id=m.id
    JOIN import_session i ON i.id=s.session_id
    GROUP BY m.id
    ORDER BY coalesce(nullif((SELECT corrected_time FROM user_annotation a WHERE a.media_id=m.id AND corrected_time IS NOT NULL ORDER BY id DESC LIMIT 1), ''), min(s.source_mtime))
  )SQL";
  if (sqlite3_prepare_v2(db.raw, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::cout << "{\"media_id\":" << sqlite3_column_int64(stmt, 0)
              << ",\"sha256\":" << json(text_col(stmt, 1))
              << ",\"type\":" << json(text_col(stmt, 2))
              << ",\"size\":" << sqlite3_column_int64(stmt, 3)
              << ",\"observed_time\":" << json(text_col(stmt, 4))
              << ",\"confidence\":" << json(text_col(stmt, 5))
              << ",\"timezone\":" << json(text_col(stmt, 6))
              << ",\"corrected_time\":" << json(text_col(stmt, 7))
              << ",\"devices\":" << json(text_col(stmt, 8))
              << ",\"provenance_count\":" << sqlite3_column_int64(stmt, 9) << "}\n";
  }
  sqlite3_finalize(stmt);
}

static void search(Db &db, const std::string &q, int limit) {
  if (limit < 1 || limit > 500) throw std::runtime_error("search limit must be between 1 and 500");
  const auto match = fts_query(q);
  sqlite3_stmt *stmt = nullptr;
  std::string sql = match.empty() ? R"SQL(
    SELECT m.id, m.sha256, group_concat(DISTINCT i.layout), min(s.source_mtime), m.media_type
    FROM media_object m JOIN media_source s ON s.media_id=m.id JOIN import_session i ON i.id=s.session_id
    GROUP BY m.id ORDER BY min(s.source_mtime) LIMIT ?
  )SQL" : R"SQL(
    SELECT m.id, m.sha256, group_concat(DISTINCT i.layout), min(s.source_mtime), m.media_type
    FROM search_document d JOIN media_object m ON m.id=d.media_id JOIN media_source s ON s.media_id=m.id JOIN import_session i ON i.id=s.session_id
    WHERE search_document MATCH ?
    GROUP BY m.id ORDER BY rank LIMIT ?
  )SQL";
  if (sqlite3_prepare_v2(db.raw, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  if (match.empty()) bind_value(stmt, 1, static_cast<std::int64_t>(limit));
  else {
    bind_value(stmt, 1, match);
    bind_value(stmt, 2, static_cast<std::int64_t>(limit));
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    std::cout << "{\"media_id\":" << sqlite3_column_int64(stmt, 0)
              << ",\"sha256\":" << json(text_col(stmt, 1))
              << ",\"devices\":" << json(text_col(stmt, 2))
              << ",\"observed_time\":" << json(text_col(stmt, 3))
              << ",\"type\":" << json(text_col(stmt, 4)) << "}\n";
  }
  sqlite3_finalize(stmt);
}

static void annotate(Db &db, std::int64_t media_id, const std::string &corrected, const std::string &tags, const std::string &notes, const std::string &actor) {
  if (!one(db, "SELECT sha256 FROM media_object WHERE id=?", media_id)) throw std::runtime_error("unknown media id");
  run(db, "INSERT INTO user_annotation(media_id,corrected_time,tags,notes,actor) VALUES(?,?,?,?,?)", media_id, corrected, tags, notes, actor);
  run(db, "INSERT INTO audit_event(event,detail) VALUES(?,?)", "annotation_added", std::to_string(media_id));
  refresh_search(db, media_id);
  std::cout << "{\"event\":\"annotation_added\",\"trace_id\":" << json(trace_id()) << ",\"media_id\":" << media_id << "}\n";
}

static void preview(Db &db, std::int64_t media_id, const fs::path &asset_root, std::int64_t max_bytes) {
  const auto stored = one(db, "SELECT stored_path FROM media_object WHERE id=?", media_id);
  if (!stored) throw std::runtime_error("unknown media id");
  fs::path out = asset_root / "preview" / (std::to_string(media_id) + ".bin");
  fs::create_directories(out.parent_path());
  std::ifstream in(*stored, std::ios::binary);
  std::ofstream dst(out, std::ios::binary | std::ios::trunc);
  std::array<char, 4096> buf{};
  std::int64_t written = 0;
  while (in && written < max_bytes) {
    auto want = std::min<std::int64_t>(buf.size(), max_bytes - written);
    in.read(buf.data(), want);
    if (in.gcount()) {
      dst.write(buf.data(), in.gcount());
      written += in.gcount();
    }
  }
  run(db, "INSERT INTO derived_asset(media_id,kind,path,size) VALUES(?,?,?,?)", media_id, "bounded-preview", out.string(), written);
  std::cout << "{\"event\":\"preview_created\",\"trace_id\":" << json(trace_id()) << ",\"media_id\":" << media_id << ",\"bytes\":" << written << "}\n";
}

static void grant(Db &db, std::int64_t media_id) {
  if (!one(db, "SELECT sha256 FROM media_object WHERE id=?", media_id)) throw std::runtime_error("unknown media id");
  std::array<unsigned char, 32> token_bytes{};
  if (RAND_bytes(token_bytes.data(), token_bytes.size()) != 1) throw std::runtime_error("grant generation failed");
  const auto token = hex(token_bytes.data(), token_bytes.size());
  run(db, "INSERT INTO playback_grant(token,media_id,expires_at) VALUES(?,?,datetime('now','+5 minutes'))", token, media_id);
  std::cout << "{\"token\":" << json(token) << ",\"media_id\":" << media_id << ",\"expires_in_seconds\":300}\n";
}

static void read_range(Db &db, const std::string &token, std::int64_t start, std::int64_t length) {
  if (length > 1024 * 1024) throw std::runtime_error("range too large");
  sqlite3_stmt *stmt = nullptr;
  const char *sql = R"SQL(
    SELECT m.stored_path, m.size FROM playback_grant g JOIN media_object m ON m.id=g.media_id
    WHERE g.token=? AND g.expires_at > datetime('now')
  )SQL";
  if (sqlite3_prepare_v2(db.raw, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db.raw));
  bind_value(stmt, 1, token);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    throw std::runtime_error("invalid playback grant");
  }
  fs::path path = text_col(stmt, 0);
  const auto size = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 1));
  sqlite3_finalize(stmt);
  if (start < 0 || length < 1 || start >= size) throw std::runtime_error("invalid range");
  std::ifstream in(path, std::ios::binary);
  in.seekg(start);
  std::array<char, 64 * 1024> buf{};
  auto left = std::min(length, size - start);
  while (in && left > 0) {
    auto want = std::min<std::int64_t>(buf.size(), left);
    in.read(buf.data(), want);
    if (!in.gcount()) break;
    std::cout.write(buf.data(), in.gcount());
    left -= in.gcount();
  }
}

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      std::cerr << "usage: locallens <db> import <mount-root> <store-root> | status | health <store-root> | version | timeline | search <query> [limit] | annotate <media-id> <corrected-time> <tags> <notes> <actor> | preview <media-id> <asset-root> <max-bytes> | grant <media-id> | read <token> <start> <length> | backup <store-root> <remote-root> <recovery-key> | verify-backup <remote-root> <recovery-key> <manifest-id> | restore <remote-root> <restore-root> <recovery-key> <manifest-id>\n";
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
    if (cmd == "health" && argc == 4) {
      health(db, argv[3]);
      return 0;
    }
    if (cmd == "version" && argc == 3) {
      std::cout << LOCALLENS_VERSION << "\n";
      return 0;
    }
    if (cmd == "timeline" && argc == 3) {
      timeline(db);
      return 0;
    }
    if (cmd == "search" && (argc == 4 || argc == 5)) {
      search(db, argv[3], argc == 5 ? std::stoi(argv[4]) : 25);
      return 0;
    }
    if (cmd == "annotate" && argc == 8) {
      annotate(db, std::stoll(argv[3]), argv[4], argv[5], argv[6], argv[7]);
      return 0;
    }
    if (cmd == "preview" && argc == 6) {
      preview(db, std::stoll(argv[3]), argv[4], std::stoll(argv[5]));
      return 0;
    }
    if (cmd == "grant" && argc == 4) {
      grant(db, std::stoll(argv[3]));
      return 0;
    }
    if (cmd == "read" && argc == 6) {
      read_range(db, argv[3], std::stoll(argv[4]), std::stoll(argv[5]));
      return 0;
    }
    if (cmd == "backup" && argc == 6) {
      backup(db, argv[3], argv[4], argv[5]);
      return 0;
    }
    if (cmd == "verify-backup" && argc == 6) {
      verify_backup(db, argv[3], argv[4], argv[5]);
      return 0;
    }
    if (cmd == "restore" && argc == 7) {
      restore_backup(db, argv[3], argv[4], argv[5], argv[6]);
      return 0;
    }
    std::cerr << "invalid command\n";
    return 2;
  } catch (const std::exception &) {
    std::cerr << "{\"event\":\"fatal\",\"trace_id\":" << json(trace_id()) << ",\"code\":\"operation_failed\"}\n";
    return 1;
  }
}
