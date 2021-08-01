#pragma once
#include <string>
#include <optional> 

#include "utils.h"

class Path {
private:
	std::wstring canon_path;

public:
	Path();
	Path(const std::wstring& pathname);

	void parts(std::vector<std::wstring> &res) const;
	Path slash(const std::wstring& suffix) const;

	std::optional<std::wstring> filename() const;
	Path parent() const;

	std::wstring get_path() const;
	std::string get_path_utf8() const;
	void create_directories();
	bool exists() const;
	Path add_redundant_dot() const;

};
std::wostream& operator<<(std::wostream& stream, const Path& path);

void copy_file(Path src, Path dst);