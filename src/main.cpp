#include <SDL.h>
#include <SDL_opengl.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

using namespace std;

struct DateTime
{
    char day[2];
    char period1;
    char month[2];
    char period2;
    char year[4];
    char space;
    char hour[2];
    char colon1;
    char minute[2];
    char colon2;
    char second[2];
    char period3;
    char millisecond[3];

    uint64_t from_digits(const char (&ch)[2])
    {
        return (ch[0] - '0') * 10 + (ch[1] - '0');
    }

    uint64_t from_digits(const char (&ch)[3])
    {
        return (ch[0] - '0') * 100 + (ch[1] - '0') * 10 + (ch[2] - '0');
    }

    uint64_t from_digits(const char (&ch)[4])
    {
        return (ch[0] - '0') * 1000 + (ch[1] - '0') * 100 + (ch[2] - '0') * 10 + (ch[3] - '0');
    }

    // clang-format off
    uint64_t toUint()
    {
        return from_digits(year) * 365ULL * 24ULL * 60ULL * 60ULL * 1000ULL +
               from_digits(month) * 31ULL * 24ULL * 60ULL * 60ULL * 1000ULL +
               from_digits(day) * 24ULL * 60ULL * 60ULL * 1000ULL +
               from_digits(hour) * 60ULL * 60ULL * 1000ULL +
               from_digits(minute) * 60ULL * 1000ULL +
               from_digits(second) * 1000ULL +
               from_digits(millisecond);
    }
    // clang-format on
};

struct LineData
{
    uint64_t time = 0;
    uint32_t flags = 0;
    char* whole_line = nullptr;
};

const int SOURCE_NAME_BUFFER_MAX_SIZE = 100;
const int SEARCH_SUBSTRING_BUFFER_MAX_SIZE = 100;
const int DATE_BUFFER_MAX_SIZE = 24;

struct SourceFlagData
{
    char source_name[SOURCE_NAME_BUFFER_MAX_SIZE] = { 0 };
    uint32_t source_flag = 0; // 0 means it is unset
};

struct SourceFlagHashTable
{
    static const int TABLE_MAX_SIZE = 100;

    // We start at 1 << 3 because the first 3 bits
    // are occupied by 3 log level flags
    uint32_t flag_generator = 1 << 3;

    SourceFlagData table[TABLE_MAX_SIZE];
};

struct FileData
{
    vector<char> buffer;
    vector<LineData> line_data;
    SourceFlagHashTable source_flag_table;
};

struct SearchData
{
    uint64_t start_ms = 0;
    uint64_t end_ms = ~0ULL;
    uint64_t search_flags = 0;
    char substring[SEARCH_SUBSTRING_BUFFER_MAX_SIZE] = { 0 };

    SearchData() = default;

    SearchData(const SearchData& other)
    {
        *this = other;
    }

    void operator=(const SearchData& other)
    {
        strcpy(substring, other.substring);
        start_ms = other.start_ms;
        end_ms = other.end_ms;
        search_flags = other.search_flags;
    }

    bool operator!=(const SearchData& other)
    {
        return start_ms != other.start_ms || end_ms != other.end_ms ||
               search_flags != other.search_flags || strcmp(substring, other.substring);
    }
};

struct LogLevelUIState
{
    bool show_info = true;
    bool show_debug = true;
    bool show_critical = true;
};

struct SourceUIState
{
    string name;
    uint32_t flag = 0;
    bool show = true;
};

vector<SourceUIState> source_flag_hash_table_to_sources_ui_state(SourceFlagHashTable& t)
{
    vector<SourceUIState> result;

    for (int i = 0; i < t.TABLE_MAX_SIZE; ++i)
    {
        if (t.table[i].source_flag != 0)
        {
            SourceUIState state;
            state.name = t.table[i].source_name;
            state.flag = t.table[i].source_flag;
            state.show = true;
            result.push_back(state);
        }
    }

    sort(result.begin(), result.end(), [](const SourceUIState& l, const SourceUIState& r) {
        return l.name < r.name;
    });

    return result;
}

struct FilterByDateUIState
{
    char start_date[DATE_BUFFER_MAX_SIZE] = "15.02.2023 00:00:00.000";
    char end_date[DATE_BUFFER_MAX_SIZE] = "15.02.2023 00:00:59.999";
};

struct PaginationUIState
{
    static const size_t MAX_PAGE_SIZE = 100;

    size_t start_index = 0;
    size_t end_index = 0;

    void reset(size_t lines)
    {
        start_index = 0;
        end_index = min(lines, MAX_PAGE_SIZE);
    }

    void next(size_t lines)
    {
        size_t new_start_index = start_index + MAX_PAGE_SIZE;
        if (new_start_index <= lines)
            start_index = new_start_index;
        end_index = min(lines, start_index + MAX_PAGE_SIZE);
    }

    void prev(size_t lines)
    {
        if (start_index >= MAX_PAGE_SIZE)
        {
            start_index -= MAX_PAGE_SIZE;
            end_index = min(lines, start_index + MAX_PAGE_SIZE);
        }
        else
            reset(lines);
    }
};

enum LogLevel : uint64_t
{
    Info = 1,
    Debug = 2,
    Critical = 4
};

inline uint64_t log_level_to_flag(char* logLevel)
{
    // Info Debug Critical
    const char firstLetter = *logLevel;
    return firstLetter == 'I' ? LogLevel::Info
                              : ((firstLetter == 'D') ? LogLevel::Debug : LogLevel::Critical);
}

inline uint64_t log_level_ui_state_to_flags(const LogLevelUIState& state)
{
    const uint64_t info = LogLevel::Info * state.show_info;
    const uint64_t debug = LogLevel::Debug * state.show_debug;
    const uint64_t critical = LogLevel::Critical * state.show_critical;

    return info | debug | critical;
}

inline uint32_t source_to_flag(char* source, SourceFlagHashTable& t)
{
    uint32_t hash = 0;

    char buffer[SOURCE_NAME_BUFFER_MAX_SIZE] = { 0 };

    for (int i = 0; source[i] != ']'; ++i)
    {
        assert(i < SOURCE_NAME_BUFFER_MAX_SIZE && "Source name buffer size exceeded");
        buffer[i] = source[i];
        hash = (hash ^ source[i]) * 0x1000193;
    }

    uint32_t i = hash % t.TABLE_MAX_SIZE;

    for (; i < t.TABLE_MAX_SIZE; ++i)
    {
        if (t.table[i].source_flag == 0)
        {
            strcpy(t.table[i].source_name, buffer);
            assert(table.flag_generator != 0 && "We ran out of flags");
            t.table[i].source_flag = t.flag_generator;
            t.flag_generator <<= 1;
            return t.table[i].source_flag;
        }
        else if (strcmp(t.table[i].source_name, buffer) == 0)
            return t.table[i].source_flag;
    }

    assert(i < t.TABLE_MAX_SIZE && "Hash table is full");
    return 0;
}

inline uint64_t sources_ui_state_to_flags(const vector<SourceUIState>& states)
{
    uint32_t acc = 0;

    for (const SourceUIState& state : states)
    {
        acc |= state.flag * state.show;
    }

    return acc;
}

FileData build_file_data(const string& fileName)
{
    ifstream file(fileName, ios::binary | ios::ate);

    if (!file)
        throw runtime_error("File doesn't exist " + fileName);

    const streamsize size = file.tellg();
    file.seekg(0, ios::beg);

    FileData file_data;
    vector<char> buffer(size);

    const clock_t time_start = clock();

    if (!file.read(buffer.data(), size))
        throw runtime_error("Can't read file " + fileName);

    file.close();

    for (size_t i = 0; i < size; ++i)
    {
        char* line_start = &buffer[i];

        LineData ld;

        ++i; // skip [

        DateTime& d = *reinterpret_cast<DateTime*>(&buffer[i]);
        ld.time = d.toUint();
        ld.whole_line = line_start;

        ++i;                   // skip ]
        i += sizeof(DateTime); // skip datetime
        ++i;                   // skip [

        ld.flags |= log_level_to_flag(&buffer[i]);

        while (i < size && buffer[i] != ']')
            ++i; // skip until ]
        ++i;     // skip ]
        ++i;     // skip [

        ld.flags |= source_to_flag(&buffer[i], file_data.source_flag_table);

        file_data.line_data.push_back(ld);

        while (i < size && buffer[i] != '\n')
            ++i;

        buffer[i] = '\0';
    }

    file_data.buffer = move(buffer);

    const clock_t time_end = clock();

    cout << "File size: " << size / (1000 * 1000) << " megabytes" << endl;
    cout << "Reading and parsing took " << (time_end - time_start) / (double) CLOCKS_PER_SEC
         << " sec" << endl;

    return file_data;
}

void search(const SearchData& search_data, const FileData& file_data, vector<char*>& output)
{
    const clock_t time_start = clock();

    output.clear();

    const uint64_t search_flags = search_data.search_flags;

    const auto& line_data = file_data.line_data;

    for (size_t i = 0, size = line_data.size(); i < size; ++i)
    {
        const uint64_t time = line_data[i].time;
        const uint64_t flags = line_data[i].flags;
        const uint64_t within_time_start = time >= search_data.start_ms;
        const uint64_t within_time_end = time <= search_data.end_ms;
        const uint64_t has_flags = (search_flags & flags) == flags;

        if (within_time_start & within_time_end & has_flags)
        {
            if (strstr(line_data[i].whole_line, search_data.substring))
                output.push_back(line_data[i].whole_line);
        }
    }

    const clock_t time_end = clock();

    cout << "Full dataset search time: " << (time_end - time_start) / (double) CLOCKS_PER_SEC
         << " sec" << endl;
}

bool validate_input_string(const char* input)
{
    static const regex r("^\\d{2}\\.\\d{2}\\.\\d{4}\\s\\d{2}:\\d{2}:\\d{2}\\.\\d{3}$");
    return regex_match(input, r);
}

int main()
{
    // Initialization
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                                     SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Main loop

    FileData file_data = build_file_data("example.log");
    atomic<bool> data_ready = true;
    vector<char*> output;
    SearchData saved_default_data;
    PaginationUIState page;
    FilterByDateUIState date;
    LogLevelUIState log_level_UI_state;
    vector<SourceUIState> sources_UI_state = source_flag_hash_table_to_sources_ui_state(
        file_data.source_flag_table);

    char substring_text[SEARCH_SUBSTRING_BUFFER_MAX_SIZE] = { 0 };

    for (bool done = false; !done;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        SearchData current_search_data = saved_default_data;

        ImGui::Begin("Filtering:");

        ImGui::Text("%s", "Log categories:");

        ImGui::Checkbox("Info", &log_level_UI_state.show_info);
        ImGui::Checkbox("Debug", &log_level_UI_state.show_debug);
        ImGui::Checkbox("Critical", &log_level_UI_state.show_critical);

        ImGui::Text("%s", "");

        ImGui::Text("%s", "Date:");
        if (ImGui::InputText("From", date.start_date, DATE_BUFFER_MAX_SIZE,
                             ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::InputText("To", date.end_date, DATE_BUFFER_MAX_SIZE,
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (validate_input_string(date.start_date) && validate_input_string(date.end_date))
            {
                current_search_data.end_ms = reinterpret_cast<DateTime*>(date.end_date)->toUint();
                current_search_data.start_ms = reinterpret_cast<DateTime*>(date.start_date)->toUint();
            }
        }

        ImGui::Text("%s", "");

        ImGui::Text("%s", "Substring:");

        if (ImGui::InputText("Enter", substring_text, SEARCH_SUBSTRING_BUFFER_MAX_SIZE,
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            strcpy(current_search_data.substring, substring_text);
        }

        ImGui::Text("%s", "");

        ImGui::Text("%s", "Sources:");

        for (SourceUIState& s : sources_UI_state)
            ImGui::Checkbox(s.name.c_str(), &s.show);

        current_search_data.search_flags = 0;
        current_search_data.search_flags |= log_level_ui_state_to_flags(log_level_UI_state);
        current_search_data.search_flags |= sources_ui_state_to_flags(sources_UI_state);

        if (saved_default_data != current_search_data)
        {
            saved_default_data = current_search_data;
            data_ready = false;
            thread search_thread([&data_ready, current_search_data, &file_data, &output, &page]() {
                search(current_search_data, file_data, output);
                page.reset(output.size());
                data_ready = true;
            });
            search_thread.detach();
        }

        ImGui::End();

        ImGui::Begin("Messages");

        if (ImGui::Button("Prev"))
        {
            if (data_ready)
                page.prev(output.size());
        }

        ImGui::SameLine();

        if (ImGui::Button("Next"))
        {
            if (data_ready)
                page.next(output.size());
        }

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS) Lines %lu, Lines from %lu to %lu",
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate, output.size(),
                    page.start_index, page.end_index);

        if (data_ready)
        {
            for (size_t i = page.start_index, end = page.end_index; i < end; ++i)
                ImGui::Text("%s", output[i]);
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        const ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
        glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                     clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
