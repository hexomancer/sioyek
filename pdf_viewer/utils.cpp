//#include <Windows.h>
#include <cwctype>

#include <cassert>
#include "utils.h"
#include <optional>
#include <string>
#include <qclipboard.h>
#include <qguiapplication.h>
#include <qprocess.h>
#include <qdesktopservices.h>
#include <qurl.h>
#include <qmessagebox.h>
#include <qbytearray.h>
#include <qdatastream.h>
#include <qstringlist.h>
#include <qcommandlineparser.h>
#include <qdir.h>

extern std::wstring LIBGEN_ADDRESS;
extern std::wstring GOOGLE_SCHOLAR_ADDRESS;
#ifdef Q_OS_WIN
#include <windows.h>
#endif

std::wstring to_lower(const std::wstring& inp) {
	std::wstring res;
	for (char c : inp) {
		res.push_back(::tolower(c));
	}
	return res;
}

void convert_toc_tree(fz_outline* root, std::vector<TocNode*>& output, fz_context* context, fz_document* doc) {
	// convert an fz_outline structure to a tree of TocNodes

	do {
		if (root == nullptr) {
			break;
		}

		TocNode* current_node = new TocNode;
		current_node->title = utf8_decode(root->title);
		current_node->x = root->x;
		current_node->y = root->y;

		if (root->page == -1) { 
			// in reflowable documents such as epub we can't have static page numbers and
			// mupdf has to resolve the link
			float xp, yp;
			fz_location loc = fz_resolve_link(context, doc, root->uri, &xp, &yp);
			int chapter_page = 0;
			current_node->page = chapter_page + loc.page;
		}
		else {
			current_node->page = root->page;
		}
		convert_toc_tree(root->down, current_node->children, context, doc);

		output.push_back(current_node);
	} while (root = root->next);
}

void get_flat_toc(const std::vector<TocNode*>& roots, std::vector<std::wstring>& output, std::vector<int>& pages) {
	// Enumerate ToC nodes in DFS order

	for (const auto& root : roots) {
		output.push_back(root->title);
		pages.push_back(root->page);
		get_flat_toc(root->children, output, pages);
	}
}

TocNode* get_toc_node_from_indices_helper(const std::vector<TocNode*>& roots, const std::vector<int>& indices, int pointer) {
	assert(pointer >= 0);

	if (pointer == 0) {
		return roots[indices[pointer]];
	}

	return get_toc_node_from_indices_helper(roots[indices[pointer]]->children, indices, pointer - 1);
}

TocNode* get_toc_node_from_indices(const std::vector<TocNode*>& roots, const std::vector<int>& indices) {
	return get_toc_node_from_indices_helper(roots, indices, indices.size() - 1);
}


QStandardItem* get_item_tree_from_toc_helper(const std::vector<TocNode*>& children, QStandardItem* parent) {

	for (const auto* child : children) {
		QStandardItem* child_item = new QStandardItem(QString::fromStdWString(child->title));
		child_item->setData(child->page);
		parent->appendRow(get_item_tree_from_toc_helper(child->children, child_item));
	}
	return parent;
}


QStandardItemModel* get_model_from_toc(const std::vector<TocNode*>& roots) {

	QStandardItemModel* model = new QStandardItemModel();
	get_item_tree_from_toc_helper(roots, model->invisibleRootItem());
	return model;
}


int mod(int a, int b)
{
	// compute a mod b handling negative numbers "correctly"
	return (a % b + b) % b;
}

bool intersects(float range1_start, float range1_end, float range2_start, float range2_end) {
	if (range2_start > range1_end || range1_start > range2_end) {
		return false;
	}
	return true;
}

void parse_uri(std::string uri, int* page, float* offset_x, float* offset_y) {
	int comma_index = -1;

	uri = uri.substr(1, uri.size() - 1);
	comma_index = static_cast<int>(uri.find(","));
	*page = atoi(uri.substr(0, comma_index ).c_str());

	uri = uri.substr(comma_index+1, uri.size() - comma_index-1);
	comma_index = static_cast<int>(uri.find(","));
	*offset_x = atof(uri.substr(0, comma_index ).c_str());

	uri = uri.substr(comma_index+1, uri.size() - comma_index-1);
	*offset_y = atof(uri.c_str());
}

bool includes_rect(fz_rect includer, fz_rect includee) {
	fz_rect intersection = fz_intersect_rect(includer, includee);
	if (intersection.x0 == includee.x0 && intersection.x1 == includee.x1 &&
		intersection.y0 == includee.y0 && intersection.y1 == includee.y1) {
		return true;
	}
	return false;
}

char get_symbol(int key, bool is_shift_pressed) {
	//char key = SDL_GetKeyFromScancode(scancode);
	if (key >= 'A' && key <= 'Z') {
		if (is_shift_pressed) {
			return key;
		}
		else {
			return key + 'a' - 'A';
		}
	}

	if (key >= '0' && key <= '9') {
		return key;
	}

	return 0;
}

void rect_to_quad(fz_rect rect, float quad[8]) {
	quad[0] = rect.x0;
	quad[1] = rect.y0;
	quad[2] = rect.x1;
	quad[3] = rect.y0;
	quad[4] = rect.x0;
	quad[5] = rect.y1;
	quad[6] = rect.x1;
	quad[7] = rect.y1;
}

fz_rect corners_to_rect(fz_point corner1, fz_point corner2) {
	fz_rect res;
	res.x0 = std::min(corner1.x, corner2.x);
	res.x1 = std::max(corner1.x, corner2.x);

	res.y0 = std::min(corner1.y, corner2.y);
	res.y1 = std::max(corner1.y, corner2.y);
	return res;
}

void copy_to_clipboard(const std::wstring& text) {
	auto clipboard = QGuiApplication::clipboard();
	auto qtext = QString::fromStdWString(text);
	clipboard->setText(qtext);
}

#define OPEN_KEY(parent, name, ptr) \
	RegCreateKeyExA(parent, name, 0, 0, 0, KEY_WRITE, 0, &ptr, 0)

#define SET_KEY(parent, name, value) \
	RegSetValueExA(parent, name, 0, REG_SZ, (const BYTE *)(value), (DWORD)strlen(value) + 1)

void install_app(const char *argv0)
{
#ifdef Q_OS_WIN
	char buf[512];
	HKEY software, classes, testpdf, dotpdf;
	HKEY shell, open, command, supported_types;
	HKEY pdf_progids;

	OPEN_KEY(HKEY_CURRENT_USER, "Software", software);
	OPEN_KEY(software, "Classes", classes);
	OPEN_KEY(classes, ".pdf", dotpdf);
	OPEN_KEY(dotpdf, "OpenWithProgids", pdf_progids);
	OPEN_KEY(classes, "Sioyek", testpdf);
	OPEN_KEY(testpdf, "SupportedTypes", supported_types);
	OPEN_KEY(testpdf, "shell", shell);
	OPEN_KEY(shell, "open", open);
	OPEN_KEY(open, "command", command);

	sprintf(buf, "\"%s\" \"%%1\"", argv0);

	SET_KEY(open, "FriendlyAppName", "Sioyek");
	SET_KEY(command, "", buf);
	SET_KEY(supported_types, ".pdf", "");
	SET_KEY(pdf_progids, "sioyek", "");

	RegCloseKey(dotpdf);
	RegCloseKey(testpdf);
	RegCloseKey(classes);
	RegCloseKey(software);
#endif
}

int get_f_key(std::string name) {
	if (name[0] == '<') {
		name = name.substr(1, name.size() - 2);
	}
	if (name[0] != 'f') {
		return 0;
	}
	name = name.substr(1, name.size() - 1);
	if (!isdigit(name[0])) {
		return 0;
	}

	int num;
	std::stringstream ss(name);
	ss >> num;
	return  num;
}

void show_error_message(const std::wstring& error_message) {
	QMessageBox msgBox;
	msgBox.setText(QString::fromStdWString(error_message));
	msgBox.setStandardButtons(QMessageBox::Ok);
	msgBox.setDefaultButton(QMessageBox::Ok);
	msgBox.exec();
}

std::wstring utf8_decode(const std::string& encoded_str) {
	std::wstring res;
	utf8::utf8to32(encoded_str.begin(), encoded_str.end(), std::back_inserter(res));
	return res;
}

std::string utf8_encode(const std::wstring& decoded_str) {
	std::string res;
	utf8::utf32to8(decoded_str.begin(), decoded_str.end(), std::back_inserter(res));
	return res;
}

bool is_rtl(int c){
  if (
    (c==0x05BE)||(c==0x05C0)||(c==0x05C3)||(c==0x05C6)||
    ((c>=0x05D0)&&(c<=0x05F4))||
    (c==0x0608)||(c==0x060B)||(c==0x060D)||
    ((c>=0x061B)&&(c<=0x064A))||
    ((c>=0x066D)&&(c<=0x066F))||
    ((c>=0x0671)&&(c<=0x06D5))||
    ((c>=0x06E5)&&(c<=0x06E6))||
    ((c>=0x06EE)&&(c<=0x06EF))||
    ((c>=0x06FA)&&(c<=0x0710))||
    ((c>=0x0712)&&(c<=0x072F))||
    ((c>=0x074D)&&(c<=0x07A5))||
    ((c>=0x07B1)&&(c<=0x07EA))||
    ((c>=0x07F4)&&(c<=0x07F5))||
    ((c>=0x07FA)&&(c<=0x0815))||
    (c==0x081A)||(c==0x0824)||(c==0x0828)||
    ((c>=0x0830)&&(c<=0x0858))||
    ((c>=0x085E)&&(c<=0x08AC))||
    (c==0x200F)||(c==0xFB1D)||
    ((c>=0xFB1F)&&(c<=0xFB28))||
    ((c>=0xFB2A)&&(c<=0xFD3D))||
    ((c>=0xFD50)&&(c<=0xFDFC))||
    ((c>=0xFE70)&&(c<=0xFEFC))||
    ((c>=0x10800)&&(c<=0x1091B))||
    ((c>=0x10920)&&(c<=0x10A00))||
    ((c>=0x10A10)&&(c<=0x10A33))||
    ((c>=0x10A40)&&(c<=0x10B35))||
    ((c>=0x10B40)&&(c<=0x10C48))||
    ((c>=0x1EE00)&&(c<=0x1EEBB))
  ) return true;
  return false;
}

std::wstring reverse_wstring(const std::wstring& inp) {
	std::wstring res;
	for (int i = inp.size() - 1; i >= 0; i--) {
		res.push_back(inp[i]);
	}
	return res;
}

bool parse_search_command(const std::wstring& search_command, int* out_begin, int* out_end, std::wstring* search_text) {
	std::wstringstream ss(search_command);
	if (search_command[0] == '<') {
		wchar_t dummy;
		ss >> dummy;
		ss >> *out_begin;
		ss >> dummy;
		ss >> *out_end;
		ss >> dummy;
		std::getline(ss, *search_text);
		return true;
	}
	else {
		*search_text = ss.str();
		return false;
	}
}

float dist_squared(fz_point p1, fz_point p2) {
	return (p1.x - p2.x) * (p1.x - p2.x) + 100 * (p1.y - p2.y) * (p1.y - p2.y);
}


void get_flat_chars_from_stext_page(fz_stext_page* stext_page, std::vector<fz_stext_char*>& flat_chars) {

	LL_ITER(block, stext_page->first_block) {
		if (block->type == FZ_STEXT_BLOCK_TEXT) {
			LL_ITER(line, block->u.t.first_line) {
				LL_ITER(ch, line->first_char) {
					flat_chars.push_back(ch);
				}

			}
		}
	}
}

//void get_flat_chars_from_stext_page_with_space(fz_stext_page* stext_page, std::vector<fz_stext_char*>& flat_chars, fz_stext_char* space) {
//
//	LL_ITER(block, stext_page->first_block) {
//		if (block->type == FZ_STEXT_BLOCK_TEXT) {
//			LL_ITER(line, block->u.t.first_line) {
//				LL_ITER(ch, line->first_char) {
//					flat_chars.push_back(ch);
//				}
//				flat_chars.push_back(space);
//
//			}
//		}

//	}
//}

//fz_stext_char* find_closest_char_to_document_point(fz_stext_page* stext_page, fz_point document_point, int* location_index) {
fz_stext_char* find_closest_char_to_document_point(const std::vector<fz_stext_char*> flat_chars, fz_point document_point, int* location_index) {
	float min_distance = std::numeric_limits<float>::infinity();
	fz_stext_char* res = nullptr;

	int index = 0;
	for (auto current_char : flat_chars) {

		fz_point quad_center = current_char->origin;
		float distance = dist_squared(document_point, quad_center);
		if (distance < min_distance) {
			min_distance = distance;
			res = current_char;
			*location_index = index;
		}
		index++;
	}

	//LL_ITER(current_block, stext_page->first_block) {
	//	if (current_block->type == FZ_STEXT_BLOCK_TEXT) {
	//		LL_ITER(current_line, current_block->u.t.first_line) {
	//			LL_ITER(current_char, current_line->first_char) {
	//				fz_point quad_center = current_char->origin;
	//				float distance = dist_squared(document_point, quad_center);
	//				if (distance < min_distance) {
	//					min_distance = distance;
	//					res = current_char;
	//					*location_index = index;
	//				}
	//				index++;
	//			}
	//		}
	//	}
	//}

	return res;
}

bool is_separator(fz_stext_char* last_char, fz_stext_char* current_char) {
	if (last_char == nullptr) {
		return false;
	}

	if (current_char->c == ' ') {
		return true;
	}
	float dist = abs(last_char->quad.ll.y - current_char->quad.ll.y);
	if (dist > 1.0f) {
		return true;
	}
	return false;
}

void get_stext_page_string(fz_stext_page* page, std::wstring& res) {
	LL_ITER(block, page->first_block) {
		if (block->type != FZ_STEXT_BLOCK_TEXT) {
			continue;
		}

		get_stext_block_string(block, res);
	}
}
void get_stext_block_string(fz_stext_block* block, std::wstring& res) {
	assert(block->type == FZ_STEXT_BLOCK_TEXT);

	LL_ITER(line, block->u.t.first_line) {
		LL_ITER(c, line->first_char) {
			res.push_back(c->c);
		}
	}
}

std::wstring get_string_from_stext_line(fz_stext_line* line) {

	std::wstring res;
	LL_ITER(ch, line->first_char) {
		res.push_back(ch->c);
	}
	return res;
}

std::wstring get_string_from_stext_block(fz_stext_block* block) {

	std::wstring res;
	LL_ITER(line, block->u.t.first_line) {
		if (res.size() > 0) {
			res.append(L"\n");
		}
		res.append(get_string_from_stext_line(line));
	}
	return res;
}
bool does_stext_block_starts_with_string(fz_stext_block* block, const std::wstring& str) {
	assert(block->type == FZ_STEXT_BLOCK_TEXT);

	if (block->u.t.first_line) {
		int index = 0;
		std::wstring line_string = get_string_from_stext_line(block->u.t.first_line);
		if (line_string.find(str) == 0) {
			return true;
		}
	}
	return false;
}

std::wstring get_figure_string_from_raw_string(const std::wstring& raw_string) {

	std::wstring res;

	bool begun = false;
	for (int i = 0; i < raw_string.size(); i++) {
		wchar_t c = raw_string[i];
		bool is_last = (raw_string.size() - 1) == i;

		if (std::iswdigit(c)) {
			begun = true;
			res.push_back(c);
		}
		else if ((c == '.') && begun) {
			if (!is_last) {
				res.push_back(c);
			}
		}
		else {
			if (begun) break;
		}
	}
	return res;
}

bool is_line_referencish(std::wstring line_text) {

	if (line_text[0] == '[') {
		std::wregex reference_regex(L"^\\[[0-9]+\\]");
		if (std::regex_search(line_text, reference_regex)) {
			return true;
		}
	}

	return false;
}

bool does_stext_block_starts_with_string_case_insensitive(fz_stext_block* block, const std::wstring &str) {

	assert(block->type == FZ_STEXT_BLOCK_TEXT);

	if (block->u.t.first_line) {
		int index = 0;
		QString line_string = QString::fromStdWString(get_string_from_stext_line(block->u.t.first_line));
		line_string = line_string.toLower();
		std::wstring std_line_string = line_string.toStdWString();

		if (std_line_string.find(str) == 0) {
			return true;
		}
	}
	return false;
}

bool is_consequtive(fz_rect rect1, fz_rect rect2) {
	float xdist = abs(rect1.x1 - rect2.x0);
	float ydist1 = abs(rect1.y0 - rect2.y0);
	float ydist2 = abs(rect1.y1 - rect2.y1);
	float ydist = std::min(ydist1, ydist2);

	float rect1_width = rect1.x1 - rect1.x0;
	float rect2_width = rect2.x1 - rect2.x0;
	float average_width = (rect1_width + rect2_width) / 2.0f;

	float rect1_height = rect1.y1 - rect1.y0;
	float rect2_height = rect2.y1 - rect2.y0;
	float average_height = (rect1_height + rect2_height) / 2.0f;

	//if (xdist < 40.0f && ydist < 40.0f) {
	//if (xdist < 20.0f && ydist < 20.0f) {
	//	return true;
	//}
	if (xdist < 2*average_width && ydist < 2*average_height) {
		return true;
	}
	//if ( ydist < 10.0f) {
	//	return true;
	//}
	return false;

}
fz_rect bound_rects(const std::vector<fz_rect>& rects) {
	// find the bounding box of some rects

	fz_rect res = rects[0];

	float average_y0 = 0.0f;
	float average_y1 = 0.0f;

	for (auto rect : rects) {
		if (res.x1 < rect.x1) {
			res.x1 = rect.x1;
		}
		if (res.x0 > rect.x0) {
			res.x0 = rect.x0;
		}

		average_y0 += rect.y0;
		average_y1 += rect.y1;
	}

	average_y0 /= rects.size();
	average_y1 /= rects.size();

	res.y0 = average_y0;
	res.y1 = average_y1;

	return res;

}
void merge_selected_character_rects(const std::vector<fz_rect>& selected_character_rects, std::vector<fz_rect>& resulting_rects) {
	/*
		This function merges the bounding boxes of all selected characters into large line chunks.
	*/

	if (selected_character_rects.size() == 0) {
		return;
	}

	std::vector<fz_rect> line_rects;

	fz_rect last_rect = selected_character_rects[0];
	line_rects.push_back(selected_character_rects[0]);

	for (int i = 1; i < selected_character_rects.size(); i++) {
		if (is_consequtive(last_rect, selected_character_rects[i])) {
			last_rect = selected_character_rects[i];
			line_rects.push_back(selected_character_rects[i]);
		}
		else {
			fz_rect bounding_rect = bound_rects(line_rects);
			resulting_rects.push_back(bounding_rect);
			line_rects.clear();
			last_rect = selected_character_rects[i];
			line_rects.push_back(selected_character_rects[i]);
		}
	}

	//for (auto rect : line_rects) {
	//	resulting_rects.push_back(rect);
	//}
	//return;

	if (line_rects.size() > 0) {
		fz_rect bounding_rect = bound_rects(line_rects);
		resulting_rects.push_back(bounding_rect);
	}

	// avoid overlapping rects
	for (int i = 0; i < resulting_rects.size() - 1; i++) {
		// we don't need to do this across columns of document
		float height = resulting_rects[i].y1 - resulting_rects[i].y0;
		if ((resulting_rects[i + 1].x0 < resulting_rects[i].x1) ) {
			resulting_rects[i + 1].y0 = resulting_rects[i].y1;
		}
	}

}

int next_sep_pos(const std::wstring& path) {
	wchar_t sep1 = '/';
	wchar_t sep2 = '\\';
	int index1 = path.find(sep1);
	int index2 = path.find(sep2);
	if (index2 == -1) {
		return index1;
	}

	if (index1 == -1) {
		return index2;
	}
	return std::min(index1, index2);
}

void split_path(std::wstring path, std::vector<std::wstring> &res) {

	size_t loc = -1;
	while ((loc = next_sep_pos(path)) != -1) {

		int skiplen = loc + 1;
		if (loc != 0) {
			std::wstring part = path.substr(0, loc);
			res.push_back(part);
		}
		path = path.substr(skiplen, path.size() - skiplen);
	}
	if (path.size() > 0) {
		res.push_back(path);
	}
}
void string_split(std::string haystack, const std::string& needle, std::vector<std::string> &res) {
	//todo: we can significantly reduce string allocations in this function if it turns out to be a 
	//performance bottleneck.

	if (haystack == needle){
		res.push_back("-");
		return;
	}

	size_t loc = -1;
	size_t needle_size = needle.size();
	while ((loc = haystack.find(needle)) != -1) {

		if ((loc < (haystack.size()-1)) &&  (haystack.substr(needle.size(), needle.size()) == needle)) {
			// if needle is repeated, one of them is added as a token for example
			// <C-->
			// means [C, -]
			res.push_back(needle);
		}

		int skiplen = loc + needle_size;
		if (loc != 0) {
			std::string part = haystack.substr(0, loc);
			res.push_back(part);
		}
		haystack = haystack.substr(skiplen, haystack.size() - skiplen);
	}
	if (haystack.size() > 0) {
		res.push_back(haystack);
	}
}


void run_command(std::wstring command, std::wstring parameters){


#ifdef Q_OS_WIN
	SHELLEXECUTEINFO ShExecInfo = { 0 };
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = NULL;
	ShExecInfo.lpFile = command.c_str();
	ShExecInfo.lpParameters = NULL;
	ShExecInfo.lpDirectory = NULL;
	ShExecInfo.nShow = SW_SHOW;
	ShExecInfo.hInstApp = NULL;
	ShExecInfo.lpParameters = parameters.c_str();

	ShellExecuteExW(&ShExecInfo);
	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
	CloseHandle(ShExecInfo.hProcess);
#else
	QProcess process;
	QString qcommand = QString::fromStdWString(command);
	QStringList qparameters; 
	qparameters.append(QString::fromStdWString(parameters));

	process.start(qcommand, qparameters);
	process.waitForFinished();
#endif

}


void open_url(const QString& url_string) {
	QDesktopServices::openUrl(url_string);
}

void open_url(const std::string &url_string) {
	QString qurl_string = QString::fromStdString(url_string);
	open_url(qurl_string);
}


void search_google_scholar(const std::wstring& search_string) {

	if (search_string.size() > 0) {
		QString qurl_string = QString::fromStdWString(GOOGLE_SCHOLAR_ADDRESS + search_string);
		open_url(qurl_string);
	}
}

void search_libgen(const std::wstring& search_string) {

	if (search_string.size() > 0) {
		QString qurl_string = QString::fromStdWString(LIBGEN_ADDRESS + search_string);
		open_url(qurl_string);
	}
}

void search_custom_engine(const std::wstring& search_string, const std::wstring& custom_engine_url) {

	if (search_string.size() > 0) {
		QString qurl_string = QString::fromStdWString(custom_engine_url + search_string);
		open_url(qurl_string);
	}
}


void open_url(const std::wstring& url_string) {

	if (url_string.size() > 0) {
		QString qurl_string = QString::fromStdWString(url_string);
		open_url(qurl_string);
	}
}

void create_file_if_not_exists(const std::wstring& path) {
	std::string path_utf8 = utf8_encode(path);
	if (!QFile::exists(QString::fromStdWString(path))) {
		std::ofstream outfile(path_utf8);
		outfile << "";
		outfile.close();
	}
}

std::wstring join_string(const std::vector<std::wstring> parts, std::wstring sep) {
	std::wstring res = L"";
	for (int i = 0; i < parts.size(); i++) {
		res.append(parts[i]);
		if (i < parts.size() - 1) {
			res.append(sep);
		}
	}
	return std::move(res);
}

std::wstring canonicalize_path(const std::wstring& path_) {
	std::wstring path  = QFileInfo(QString::fromStdWString(path_)).absoluteFilePath().toStdWString();

	std::vector<std::wstring> parts;
	split_path(path, parts);
	return std::move(join_string(parts, L"/"));
}

void open_file(const std::wstring& path) {

	//std::wstring generic_file_path = path.generic_wstring();
	std::wstring canon_path = canonicalize_path(path);
	open_url(canon_path);

}

void get_text_from_flat_chars(const std::vector<fz_stext_char*>& flat_chars, std::wstring& string_res, std::vector<int>& indices) {

	string_res.clear();
	indices.clear();

	for (int i = 0; i < flat_chars.size(); i++) {
		fz_stext_char* ch = flat_chars[i];

		if (ch->next == nullptr) { // add a space after the last character in a line, igonre hyphenated characters
			if (ch->c != '-') {
				string_res.push_back(ch->c);
				indices.push_back(i);

				string_res.push_back(' ');
				indices.push_back(-1);
			}
			continue;
		}
		string_res.push_back(ch->c);
		indices.push_back(i);
	}
}

//void get_matches(std::wstring haystack, const std::wregex& reg, std::vector<std::pair<int, int>>& indices) {
//	std::wsmatch match;
//
//	int offset = 0;
//	while (std::regex_search(haystack, match, reg)) {
//		int start_index = offset + match.position();
//		int end_index = start_index + match.length();
//		indices.push_back(std::make_pair(start_index, end_index));
//
//		int old_length = haystack.size();
//		haystack = match.suffix();
//		int new_length = haystack.size();
//
//		offset += (old_length - new_length);
//	}
//}

void find_regex_matches_in_stext_page(const std::vector<fz_stext_char*>& flat_chars,
	const std::wregex &regex,
	std::vector<std::pair<int, int>> &match_ranges, std::vector<std::wstring> &match_texts){

	std::wstring page_string;
	std::vector<int> indices;

	get_text_from_flat_chars(flat_chars, page_string, indices);

	std::wsmatch match;

	int offset = 0;
	while (std::regex_search(page_string, match, regex)) {
		int start_index = offset + match.position();
		int end_index = start_index + match.length()-1;
		match_ranges.push_back(std::make_pair(indices[start_index], indices[end_index]));
		match_texts.push_back(match.str());

		int old_length = page_string.size();
		page_string = match.suffix();
		int new_length = page_string.size();

		offset += (old_length - new_length);
	}
}

bool are_stext_chars_far_enough(fz_stext_char* first, fz_stext_char* second) {
	float second_width = second->quad.lr.x - second->quad.ll.x;
	assert(second_width > 0);

	return (second->origin.x - first->origin.x) > (5 * second_width);
}

void index_equations(const std::vector<fz_stext_char*> &flat_chars, int page_number, std::map<std::wstring, IndexedData>& indices) {
	std::wregex regex(L"\\([0-9]+(\\.[0-9]+)*\\)");
	std::vector<std::pair<int, int>> match_ranges;
	std::vector<std::wstring> match_texts;

	find_regex_matches_in_stext_page(flat_chars, regex, match_ranges, match_texts);

	for (int i = 0; i < match_ranges.size(); i++) {
		auto [start_index, end_index] = match_ranges[i];
		if (start_index == -1 || end_index == -1) {
			break;
		}
		assert(flat_chars[start_index]->c == '(');
		assert(flat_chars[end_index]->c == ')');

		// we expect the equation reference to be sufficiently separated from the rest of the text
		if ((start_index > 0) && are_stext_chars_far_enough(flat_chars[start_index-1], flat_chars[start_index])) { 
			assert(match_texts[i].size() > 2);

			std::wstring match_text = match_texts[i].substr(1, match_texts[i].size() - 2);
			IndexedData indexed_equation;
			indexed_equation.page = page_number;
			indexed_equation.text = match_text;
			indexed_equation.y_offset = flat_chars[start_index]->quad.ll.y;
			indices[match_text] = indexed_equation;
		}
	}

}

void index_references(fz_stext_page* page, int page_number, std::map<std::wstring, IndexedData>& indices) {

	char start_char = '[';
	char end_char = ']';
	char delim_char = ',';

	bool is_in_reference = false;
	const int MAX_REFERENCE_SIZE = 10;

	bool started = false;
	std::vector<IndexedData> temp_indices;
	std::wstring current_text = L"";

	LL_ITER(block, page->first_block) {
		if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

		LL_ITER(line, block->u.t.first_line) {
			LL_ITER(ch, line->first_char) {
				if (ch->c == ' ') {
					continue;
				}
				if (ch->c == '.') {
					started = false;
					temp_indices.clear();
					current_text.clear();
				}

				if (ch->c == start_char) {
					temp_indices.clear();
					current_text.clear();
					started = true;
					continue;
				}
				if (ch->c == end_char) {
					started = false;

					IndexedData index_data;
					index_data.page = page_number;
					index_data.y_offset = ch->quad.ll.y;
					index_data.text = current_text;

					temp_indices.push_back(index_data);
					//indices[text] = index_data;

					for (auto index : temp_indices) {
						indices[index.text] = index;
					}
					current_text.clear();
					temp_indices.clear();
					continue;
				}
				if (started && (ch->c == delim_char)) {
					IndexedData index_data;
					index_data.page = page_number;
					index_data.y_offset = ch->quad.ll.y;
					index_data.text = current_text;
					current_text.clear();
					temp_indices.push_back(index_data);
					continue;
				}
				if (started) {
					current_text.push_back(ch->c);
				}
			}

		}
	}
}

void sleep_ms(unsigned int ms) {
#ifdef Q_OS_WIN
	Sleep(ms);
#else
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
	nanosleep(&ts, NULL);
#endif
}

void get_pixmap_pixel(fz_pixmap* pixmap, int x, int y, unsigned char* r, unsigned char* g, unsigned char* b){
	if (
		(x < 0) ||
		(y < 0) ||
		(x >= pixmap->w) ||
		(y >= pixmap->h)
		) {
		*r = 0;
		*g = 0;
		*b = 0;
		return;
	}

	(*r) = pixmap->samples[y * pixmap->w * 3 + x * 3 + 0];
	(*g) = pixmap->samples[y * pixmap->w * 3 + x * 3 + 1];
	(*b) = pixmap->samples[y * pixmap->w * 3 + x * 3 + 2];
}

int find_max_horizontal_line_length_at_pos(fz_pixmap* pixmap, int pos_x, int pos_y) {
	int min_x = pos_x;
	int max_x = pos_x;

	while (true) {
		unsigned char r, g, b;
		get_pixmap_pixel(pixmap, min_x, pos_y, &r, &g, &b);
		if ((r != 255) || (g != 255) || (b != 255)) {
			break;
		}
		else {
			min_x--;
		}
	}
	while (true) {
		unsigned char r, g, b;
		get_pixmap_pixel(pixmap, max_x, pos_y, &r, &g, &b);
		if ((r != 255) || (g != 255) || (b != 255)) {
			break;
		}
		else {
			max_x++;
		}
	}
	return max_x - min_x;
}

bool largest_contigous_ones(std::vector<int>& arr, int* start_index, int* end_index) {
	arr.push_back(0);

	bool has_at_least_one_one = false;

	for (auto x : arr) {
		if (x == 1) {
			has_at_least_one_one = true;
		}
	}
	if (!has_at_least_one_one) {
		return false;
	}


	int max_count = 0;
	int max_start_index = -1;
	int max_end_index = -1;

	int count = 0;

	for (int i = 0; i < arr.size(); i++) {
		if (arr[i] == 1) {
			count++;
		}
		else {
			if (count > max_count) {
				max_count = count;
				max_end_index = i - 1;
				max_start_index = i - count;
			}
			count = 0;
		}
	}

	*start_index = max_start_index;
	*end_index = max_end_index;
	return true;
}

int find_best_vertical_line_location(fz_pixmap* pixmap, int doc_x, int doc_y) {


	int search_height = 5;

	float min_candid_y = doc_y;
	float max_candid_y = doc_y + search_height;

	std::vector<int> max_possible_widths;

	for (int candid_y = min_candid_y; candid_y <= max_candid_y; candid_y++) {
		int current_width = find_max_horizontal_line_length_at_pos(pixmap, doc_x , candid_y);
		max_possible_widths.push_back(current_width);
	}

	int max_width_value = -1;

	for (auto w : max_possible_widths) {
		if (w > max_width_value) {
			max_width_value = w;
		}
	}
	std::vector<int> is_max_list;

	for (auto x : max_possible_widths) {
		if (x == max_width_value) {
			is_max_list.push_back(1);
		}
		else{
			is_max_list.push_back(0);
		}
	}

	int start_index, end_index;
	largest_contigous_ones(is_max_list, &start_index, &end_index);

	int test = (start_index + end_index) / 2;
	//return doc_y + (start_index + end_index) / 2;
	return doc_y + start_index ;
}

bool is_string_numeric(const std::wstring& str) {
	if (str.size() == 0) {
		return false;
	}
	for (auto ch : str) {
		if (!std::isdigit(ch)) {
			return false;
		}
	}
	return true;
}

bool is_string_numeric_float(const std::wstring& str) {
	if (str.size() == 0) {
		return false;
	}
	int dot_count = 0;

	for (int i = 0; i < str.size(); i++) {
		if (i == 0) {
			if (str[i] == '-') continue;
		}
		else {
			if (str[i] == '.') {
				dot_count++;
				if (dot_count >= 2) return false;
			}
			else if (!std::isdigit(str[i])) {
				return false;
			}
		}

	}
	return true;
}

QByteArray serialize_string_array(const QStringList& string_list) {
	QByteArray result;
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream << string_list.size();
	for (int i = 0; i < string_list.size(); i++) {
		stream << string_list.at(i);
	}
	return result;
}


QStringList deserialize_string_array(const QByteArray &byte_array) {
	QStringList result;
	QDataStream stream(byte_array);

	int size;
	stream >> size;


	for (int i = 0; i < size; i++) {
		QString string;
		stream >> string;
		result.append(string);
	}

	return result;
}




bool should_reuse_instance(int argc, char** argv) {
	for (int i = 0; i < argc; i++) {
		if (std::strcmp(argv[i], "--reuse-instance") == 0) return true;

	}
	return false;
}

bool should_new_instance(int argc, char** argv) {
	for (int i = 0; i < argc; i++) {
		if (std::strcmp(argv[i], "--new-instance") == 0) return true;

	}
	return false;
}

QCommandLineParser* get_command_line_parser() {

	QCommandLineParser* parser = new QCommandLineParser();

	parser->setApplicationDescription("Sioyek is a PDF reader designed for reading research papers and technical books.");
	parser->addHelpOption();
	parser->addVersionOption();

	QCommandLineOption reuse_instance_option("reuse-instance");
	reuse_instance_option.setDescription("When opening a new file, reuse the previous instance of sioyek instead of opening a new window.");
	parser->addOption(reuse_instance_option);

	QCommandLineOption new_instance_option("new-instance");
	new_instance_option.setDescription("When opening a new file, create a new instacne of sioyek.");
	parser->addOption(new_instance_option);

	QCommandLineOption page_option("page", "Which page to open.", "page");
	parser->addOption(page_option);

	QCommandLineOption inverse_search_option("inverse-search", "The command to execute when performing inverse search.\
 In <command>, %1 is filled with the file name and %2 is filled with the line number.", "command");
	parser->addOption(inverse_search_option);

	QCommandLineOption forward_search_file_option("forward-search-file", "Perform forward search on file <file> must also include --forward-search-line to specify the line", "file");
	parser->addOption(forward_search_file_option);

	QCommandLineOption forward_search_line_option("forward-search-line", "Perform forward search on line <line> must also include --forward-search-file to specify the file", "file");
	parser->addOption(forward_search_line_option);

	QCommandLineOption zoom_level_option("zoom", "Set zoom level to <zoom>.", "zoom");
	parser->addOption(zoom_level_option);

	QCommandLineOption xloc_option("xloc", "Set x position within page to <xloc>.", "xloc");
	parser->addOption(xloc_option);

	QCommandLineOption yloc_option("yloc", "Set y position within page to <yloc>.", "yloc");
	parser->addOption(yloc_option);

	return parser;
}


std::wstring concatenate_path(const std::wstring& prefix, const std::wstring& suffix) {
	std::wstring result = prefix;
#ifdef Q_OS_WIN
	wchar_t separator = '\\';
#else
	wchar_t separator = '/';
#endif
	if (prefix == L"") {
		return suffix;
	}

	if (result[result.size() - 1] != separator) {
		result.push_back(separator);
	}
	result.append(suffix);
	return std::move(result);
}

std::wstring concatenate_paths(const std::vector<std::wstring>& paths) {
	std::wstring res = L"";
	for (int i = 0; i < paths.size(); i++) {
		res = concatenate_path(res, paths[i]);
	}
	return res;
}

std::wstring get_canonical_path(const std::wstring& path) {
	QDir dir(QString::fromStdWString(path));
	return std::move(dir.canonicalPath().toStdWString());

}

