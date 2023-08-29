#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <optional>
#include <utility>
#include <memory>
#include <array>

#include <qapplication.h>
#include <qpushbutton.h>
#include <qopenglwidget.h>
#include <qopenglextrafunctions.h>
#include <qopenglfunctions.h>
#include <qopengl.h>
#include <qwindow.h>
#include <qkeyevent.h>
#include <qlineedit.h>
#include <qtreeview.h>
#include <qsortfilterproxymodel.h>
#include <qabstractitemmodel.h>
#include <qopenglshaderprogram.h>
#include <qtimer.h>
#include <qdatetime.h>
#include <qstackedwidget.h>
#include <qboxlayout.h>
#include <qlistview.h>
#include <qstringlistmodel.h>
#include <qlabel.h>
#include <qtextedit.h>
#include <qfilesystemwatcher.h>

#ifndef SIOYEK_QT6
#include <qdesktopwidget.h>
#endif

#include <qpainter.h>

#include "document_view.h"
#include "path.h"


struct MarkedDataRect {
    DocumentRect rect;
    int type;
};

enum HighlightRenderFlags
{
    HRF_FILL = 1 << 0,
    HRF_BORDER = 1 << 1,
    HRF_UNDERLINE = 1 << 2,
    HRF_STRIKE = 1 << 3
};


struct OpenGLSharedResources {
    GLuint vertex_buffer_object;
    GLuint uv_buffer_object;
    GLuint line_points_buffer_object;
    GLuint rendered_program;
    GLuint rendered_dark_program;
    GLuint custom_color_program;
    GLuint unrendered_program;
    GLuint highlight_program;
    GLuint vertical_line_program;
    GLuint vertical_line_dark_program;
    GLuint separator_program;
    GLuint stencil_program;
    GLuint line_program;

    GLint dark_mode_contrast_uniform_location;
    GLint highlight_color_uniform_location;
    GLint line_color_uniform_location;
    GLint line_time_uniform_location;

    GLint gamma_uniform_location;

    GLint custom_color_transform_uniform_location;

    GLint separator_background_color_uniform_location;
    GLint freehand_line_color_uniform_location;

    bool is_initialized;
};

struct OverviewState {
    //int page;
    float absolute_offset_y;
    Document* doc = nullptr;
    //float page_height;
};

class PdfViewOpenGLWidget : public QOpenGLWidget, protected QOpenGLExtraFunctions {
public:

    enum OverviewSide {
        bottom = 0,
        top = 1,
        left = 2,
        right = 3
    };

    struct OverviewResizeData {
        fz_rect original_rect;
        NormalizedWindowPos original_normal_mouse_pos;
        OverviewSide side_index;
    };

    struct OverviewMoveData {
        fvec2 original_offsets;
        NormalizedWindowPos original_normal_mouse_pos;
    };

    struct OverviewTouchMoveData {
        float overview_original_pos_offset_y;
        float original_mouse_offset_y;

        //NormalizedWindowPos original_normal_mouse_pos;
    };

    enum ColorPalette {
        Normal,
        Dark,
        Custom
    };

    ColorPalette get_current_color_mode();
private:
    static OpenGLSharedResources shared_gl_objects;

    bool is_opengl_initialized = false;
    GLuint vertex_array_object;
    DocumentView* document_view = nullptr;
    PdfRenderer* pdf_renderer = nullptr;
    ConfigManager* config_manager = nullptr;
    std::vector<SearchResult> search_results;
    int current_search_result_index = -1;
    std::mutex search_results_mutex;
    bool is_search_cancelled = true;
    bool is_searching;
    bool should_highlight_links = false;
    bool should_highlight_words = false;
    bool should_show_numbers = false;
    bool should_show_rect_hints = false;
    ColorPalette color_mode = ColorPalette::Normal;
    bool is_helper = false;
    float percent_done = 0.0f;
    std::optional<int> visible_page_number = {};

    QIcon bookmark_icon;
    QIcon portal_icon;
    QIcon hourglass_icon;

    std::optional<fz_rect> character_highlight_rect = {};
    std::optional<fz_rect> wrong_character_rect = {};
    bool show_control_rect;

    std::optional<AbsoluteDocumentPos> underline = {};

    int rotation_index = 0;
    bool is_dragging = false;
    bool fastread_mode = false;

    int last_mouse_down_window_x = 0;
    int last_mouse_down_window_y = 0;

    float last_mouse_down_document_offset_x = 0;
    float last_mouse_down_document_offset_y = 0;
    std::vector<AbsoluteRect> pending_download_portals;

    //float vertical_line_location;
    //bool should_draw_vertical_line = false;
    QDateTime creation_time;

    std::optional<std::function<void(const OpenedBookState&)>> on_link_edit = {};
    std::optional<OverviewState> overview_page = {};

    float overview_half_width = 0.8f;
    float overview_half_height = 0.4f;

    float overview_offset_x = 0.0f;
    float overview_offset_y = 0.0f;

    std::optional<AbsoluteRect> selected_rectangle = {};

    GLuint LoadShaders(Path vertex_file_path_, Path fragment_file_path_);
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void render_highlight_window(GLuint program, NormalizedWindowRect window_rect, int flags);
    void render_highlight_absolute(GLuint program, AbsoluteRect absolute_document_rect, int flags);
    void render_line_window(GLuint program, float vertical_pos, std::optional<NormalizedWindowRect> ruler_rect = {});
    void render_highlight_document(GLuint program, DocumentRect doc_rect, int flags=HRF_FILL | HRF_BORDER);
    void paintGL() override;
    void my_render(QPainter* painter);
    void add_coordinates_for_window_point(float window_x, float window_y, float r, int point_polygon_vertices, std::vector<float>& out_coordinates);
    void render_drawings(const std::vector<FreehandDrawing>& drawings, bool highlighted = false);
    std::vector<std::pair<QRect, QString>> get_hint_rect_and_texts();

    void enable_stencil();
    void write_to_stencil();
    void draw_stencil_rects(const std::vector<fz_rect>& rects, bool is_window_rect = false, int page = -1);
    void use_stencil_to_write(bool eq);
    void disable_stencil();

    void render_transparent_background();

public:
    bool visible_drawing_mask[26];
    FreehandDrawing current_drawing;
    std::vector<FreehandDrawing> moving_drawings;

#ifndef NDEBUG
    // properties for visualizing selected blocks, used only for debug
    std::optional<int> last_selected_block_page = {};
    std::optional<fz_rect> last_selected_block = {};
#endif

    std::vector<DocumentRect> word_rects;
    std::vector<DocumentRect> synctex_highlights;
    QTime synctex_highlight_time;
    //std::vector<std::pair<fz_rect, int>> marked_data_rects;
    std::vector<MarkedDataRect> marked_data_rects;

    PdfViewOpenGLWidget(DocumentView* document_view, PdfRenderer* pdf_renderer, ConfigManager* config_manager, bool is_helper, QWidget* parent = nullptr);
    ~PdfViewOpenGLWidget();

    //void set_vertical_line_pos(float pos);
    //float get_vertical_line_pos();
    //void set_should_draw_vertical_line(bool val);
    //bool get_should_draw_vertical_line();
    void handle_escape();

    std::map<int, std::vector<MarkedDataRect>> get_marked_data_rect_map();
    void toggle_highlight_links();
    void set_highlight_links(bool should_highlight_links, bool should_show_numbers);
    void toggle_highlight_words();
    void set_highlight_words(std::vector<DocumentRect>& rects);
    void set_should_highlight_words(bool should_highlight);
    std::vector<DocumentRect> get_highlight_word_rects();

    int get_num_search_results();
    int get_current_search_result_index();
    bool valid_document();
    std::optional<SearchResult> set_search_result_offset(int offset);
    std::optional<SearchResult> get_current_search_result();
    void goto_search_result(int offset, bool overview = false);
    void render_overview(OverviewState overview);
    void render_page(int page_number);
    bool get_is_searching(float* prog);
    void search_text(const std::wstring& text, SearchCaseSensitivity case_sensitive = SearchCaseSensitivity::CaseInsensitive, bool regex = false, std::optional<std::pair<int, int>> range = {});
    void set_dark_mode(bool mode);
    void toggle_dark_mode();
    void set_custom_color_mode(bool mode);
    void toggle_custom_color_mode();
    void set_synctex_highlights(std::vector<DocumentRect> highlights);
    bool should_show_synxtex_highlights();
    bool has_synctex_timed_out();
    void on_document_view_reset();
    void mouseMoveEvent(QMouseEvent* mouse_event) override;
    void mousePressEvent(QMouseEvent* mevent) override;
    void mouseReleaseEvent(QMouseEvent* mevent) override;
    void wheelEvent(QWheelEvent* wevent) override;
    void register_on_link_edit_listener(std::function<void(const OpenedBookState&)> listener);
    void set_overview_page(std::optional<OverviewState> overview_page);
    std::optional<OverviewState> get_overview_page();
    void draw_empty_helper_message(QPainter* painter);
    void set_visible_page_number(std::optional<int> val);
    bool is_presentation_mode();
    fz_rect	get_overview_rect();
    fz_rect	get_overview_rect_pixel_perfect(int widget_width, int widget_height, int view_width, int view_height);
    std::vector<fz_rect> get_overview_border_rects();
    bool is_window_point_in_overview(fvec2 window_point);
    bool is_window_point_in_overview_border(float window_x, float window_y, OverviewSide* which_border);

    void get_overview_offsets(float* offset_x, float* offset_y);
    void get_overview_size(float* width, float* height);

    float get_overview_side_pos(int index);
    void set_overview_side_pos(int index, fz_rect original_rect, fvec2 diff);
    void set_overview_rect(fz_rect rect);

    void set_overview_offsets(float offset_x, float offset_y);
    void set_overview_offsets(fvec2 offsets);

    void bind_program();
    void cancel_search();
    //void window_pos_to_overview_pos(float window_x, float window_y, float* doc_offset_x, float* doc_offset_y, int* doc_page);
    DocumentPos window_pos_to_overview_pos(NormalizedWindowPos window_pos);
    void rotate_clockwise();
    void rotate_counterclockwise();

    bool is_rotated();
    void toggle_fastread_mode();
    void setup_text_painter(QPainter* painter);
    void get_overview_window_vertices(float out_vertices[2 * 4]);

    void set_selected_rectangle(AbsoluteRect selected);
    void clear_selected_rectangle();
    void clear_all_selections();

    std::optional<AbsoluteRect> get_selected_rectangle();

    void set_typing_rect(int page, fz_rect rect, std::optional<fz_rect> wrong_rect);

    Document* get_current_overview_document();
    NormalizedWindowPos document_to_overview_pos(DocumentPos pos);
    NormalizedWindowRect document_to_overview_rect(DocumentRect doc_rect);
    std::vector<int> get_visible_search_results(std::vector<int>& visible_pages);
    int find_search_index_for_visible_pages(std::vector<int>& visible_pages);
    int find_search_index_for_visible_page(int page, int breakpoint);
    int find_search_result_for_page_range(int page, int range_begin, int range_end);
    int find_search_results_breakpoint();
    int find_search_results_breakpoint_helper(int begin_index, int end_index);
    void get_custom_color_transform_matrix(float matrix_data[16]);
    void get_background_color(float out_background[3]);
    void set_underline(AbsoluteDocumentPos abspos);
    void clear_underline();
    void bind_points(const std::vector<float>& points);
    void bind_default();
    bool is_normalized_y_in_window(float y);
    bool is_normalized_y_range_in_window(float y0, float y1);
    void render_portal_rect(QPainter* painter, AbsoluteRect portal_absolute_rect, bool is_pending);
    void set_pending_download_portals(std::vector<AbsoluteRect>&& portal_rects);
    void show_rect_hints();
    void hide_rect_hints();
    bool is_showing_rect_hints();
    void get_color_for_current_mode(const float* input_color, float* output_color);
    std::array<float, 3> cc3(const float* input_color);
    std::array<float, 4> cc4(const float* input_color);
};
