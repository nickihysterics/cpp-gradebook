#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include "sqlite3.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct Student {
  int id = 0;
  std::string name;
  int group_id = 0;
};

struct Group {
  int id = 0;
  std::string name;
};

struct Subject {
  int id = 0;
  std::string name;
};

struct Grade {
  int id = 0;
  int student_id = 0;
  int subject_id = 0;
  int value = 0;
  int attempt = 0;
};

struct DataStore {
  std::vector<Student> students;
  std::vector<Group> groups;
  std::vector<Subject> subjects;
  std::vector<Grade> grades;
  int next_student_id = 1;
  int next_group_id = 1;
  int next_subject_id = 1;
  int next_grade_id = 1;
};

constexpr int kMinGrade = 1;
constexpr int kMaxGrade = 5;
constexpr int kPassGrade = 3;
constexpr char kCsvDelim = ';';
const char* kDataDir = "data";
const char* kExportDir = "exports";
const char* kDbFileName = "data_store.db";

std::string db_path();
std::string export_path(const std::string& filename);
void ensure_storage_dirs();
bool save_data(const DataStore& data, const std::string& path);
void autosave_or_warn(const DataStore& data);
int create_group_record(DataStore& data, const std::string& name);

// Удаляет пробелы по краям строки.
std::string trim(const std::string& input) {
  size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(start, end - start);
}

// Переводит строку в нижний регистр для простого поиска.
std::string to_lower_ascii(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (unsigned char c : input) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

// Формирует путь к базе данных относительно корня проекта.
std::string db_path() {
  return (std::filesystem::path(kDataDir) / kDbFileName).string();
}

// Формирует путь к файлу экспорта относительно корня проекта.
std::string export_path(const std::string& filename) {
  return (std::filesystem::path(kExportDir) / filename).string();
}

// Создает каталоги для хранения данных и экспортов.
void ensure_storage_dirs() {
  try {
    std::filesystem::create_directories(kDataDir);
    std::filesystem::create_directories(kExportDir);
  } catch (...) {
    std::cout << "Не удалось создать каталоги хранения.\n";
  }
}

// Читает строку из консоли с валидацией пустого ввода.
std::string read_line(const std::string& prompt, bool allow_empty = false) {
  while (true) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cout << "\nВвод закрыт.\n";
      std::exit(0);
    }
    if (allow_empty || !trim(line).empty()) {
      return line;
    }
    std::cout << "Поле не может быть пустым.\n";
  }
}

// Пробует разобрать целое число из строки.
bool parse_int(const std::string& text, int& value) {
  try {
    size_t pos = 0;
    int parsed = std::stoi(text, &pos);
    if (pos != text.size()) {
      return false;
    }
    value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Пробует разобрать число с плавающей точкой из строки.
bool parse_double(const std::string& text, double& value) {
  try {
    size_t pos = 0;
    double parsed = std::stod(text, &pos);
    if (pos != text.size()) {
      return false;
    }
    value = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Запрашивает у пользователя целое число в заданном диапазоне.
int read_int(const std::string& prompt, int min_value, int max_value) {
  while (true) {
    std::string line = trim(read_line(prompt, true));
    if (line.empty()) {
      std::cout << "Введите число.\n";
      continue;
    }
    int value = 0;
    if (!parse_int(line, value)) {
      std::cout << "Введите корректное целое число.\n";
      continue;
    }
    if (value < min_value || value > max_value) {
      std::cout << "Значение должно быть между " << min_value << " и " << max_value << ".\n";
      continue;
    }
    return value;
  }
}

// Запрашивает число, но допускает пустой ввод (false, если строка пустая).
bool read_int_optional(const std::string& prompt, int min_value, int max_value, int& out) {
  while (true) {
    std::string line = trim(read_line(prompt, true));
    if (line.empty()) {
      return false;
    }
    int value = 0;
    if (!parse_int(line, value)) {
      std::cout << "Введите корректное целое число.\n";
      continue;
    }
    if (value < min_value || value > max_value) {
      std::cout << "Значение должно быть между " << min_value << " и " << max_value << ".\n";
      continue;
    }
    out = value;
    return true;
  }
}

// Запрашивает число с плавающей точкой, допускает пустой ввод.
bool read_double_optional(const std::string& prompt, double min_value, double max_value, double& out) {
  while (true) {
    std::string line = trim(read_line(prompt, true));
    if (line.empty()) {
      return false;
    }
    double value = 0.0;
    if (!parse_double(line, value)) {
      std::cout << "Введите корректное число.\n";
      continue;
    }
    if (value < min_value || value > max_value) {
      std::cout << "Значение должно быть между " << min_value << " и " << max_value << ".\n";
      continue;
    }
    out = value;
    return true;
  }
}

// Ищет студента по ID (изменяемая версия).
Student* find_student(DataStore& data, int id) {
  for (auto& student : data.students) {
    if (student.id == id) {
      return &student;
    }
  }
  return nullptr;
}

// Ищет студента по ID (константная версия).
const Student* find_student(const DataStore& data, int id) {
  for (const auto& student : data.students) {
    if (student.id == id) {
      return &student;
    }
  }
  return nullptr;
}

// Ищет группу по ID (изменяемая версия).
Group* find_group(DataStore& data, int id) {
  for (auto& group : data.groups) {
    if (group.id == id) {
      return &group;
    }
  }
  return nullptr;
}

// Ищет группу по ID (константная версия).
const Group* find_group(const DataStore& data, int id) {
  for (const auto& group : data.groups) {
    if (group.id == id) {
      return &group;
    }
  }
  return nullptr;
}

// Ищет предмет по ID (изменяемая версия).
Subject* find_subject(DataStore& data, int id) {
  for (auto& subject : data.subjects) {
    if (subject.id == id) {
      return &subject;
    }
  }
  return nullptr;
}

// Ищет предмет по ID (константная версия).
const Subject* find_subject(const DataStore& data, int id) {
  for (const auto& subject : data.subjects) {
    if (subject.id == id) {
      return &subject;
    }
  }
  return nullptr;
}

// Ищет оценку по ID (изменяемая версия).
Grade* find_grade(DataStore& data, int id) {
  for (auto& grade : data.grades) {
    if (grade.id == id) {
      return &grade;
    }
  }
  return nullptr;
}

// Ищет оценку по ID (константная версия).
const Grade* find_grade(const DataStore& data, int id) {
  for (const auto& grade : data.grades) {
    if (grade.id == id) {
      return &grade;
    }
  }
  return nullptr;
}

// Вычисляет номер следующей попытки сдачи предмета.
int next_attempt(const DataStore& data, int student_id, int subject_id) {
  int attempt = 1;
  for (const auto& grade : data.grades) {
    if (grade.student_id == student_id && grade.subject_id == subject_id) {
      // Берем максимальный номер попытки по имеющимся оценкам.
      attempt = std::max(attempt, grade.attempt + 1);
    }
  }
  return attempt;
}

struct SubjectAggregate {
  int sum = 0;
  int count = 0;
  int latest_grade_id = 0;
  int latest_value = 0;
  int latest_attempt = 0;
};

// Собирает статистику по предметам студента (сумма, количество, последняя оценка).
std::map<int, SubjectAggregate> subject_aggregates_for_student(const DataStore& data, int student_id) {
  std::map<int, SubjectAggregate> aggregates;
  for (const auto& grade : data.grades) {
    if (grade.student_id != student_id) {
      continue;
    }
    SubjectAggregate& agg = aggregates[grade.subject_id];
    agg.sum += grade.value;
    agg.count += 1;
    if (grade.id > agg.latest_grade_id) {
      agg.latest_grade_id = grade.id;
      agg.latest_value = grade.value;
      agg.latest_attempt = grade.attempt;
    }
  }
  return aggregates;
}

// Считает средний балл студента по каждому предмету (все оценки), затем усредняет.
double average_subjects_for_student(const DataStore& data, int student_id) {
  auto aggregates = subject_aggregates_for_student(data, student_id);
  if (aggregates.empty()) {
    return -1.0;
  }
  double sum = 0.0;
  int count = 0;
  for (const auto& entry : aggregates) {
    const SubjectAggregate& agg = entry.second;
    if (agg.count > 0) {
      sum += static_cast<double>(agg.sum) / static_cast<double>(agg.count);
      ++count;
    }
  }
  if (count == 0) {
    return -1.0;
  }
  return sum / static_cast<double>(count);
}

// Считает средний балл по предмету по всем оценкам (все попытки).
double average_all_for_subject(const DataStore& data, int subject_id, int* count_out) {
  int sum = 0;
  int count = 0;
  for (const auto& grade : data.grades) {
    if (grade.subject_id == subject_id) {
      sum += grade.value;
      ++count;
    }
  }
  if (count_out) {
    *count_out = count;
  }
  if (count == 0) {
    return -1.0;
  }
  return static_cast<double>(sum) / static_cast<double>(count);
}

// Форматирует среднее значение для вывода.
std::string format_avg(double value) {
  if (value < 0.0) {
    return "нет";
  }
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << value;
  return out.str();
}

// Считает длину строки в символах UTF-8.
size_t utf8_length(const std::string& text) {
  size_t count = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) != 0x80) {
      ++count;
    }
  }
  return count;
}

// Обрезает строку до заданного количества символов UTF-8.
std::string utf8_truncate(const std::string& text, size_t max_chars) {
  if (max_chars == 0) {
    return std::string();
  }
  size_t count = 0;
  size_t i = 0;
  for (; i < text.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0) != 0x80) {
      if (count == max_chars) {
        break;
      }
      ++count;
    }
  }
  return text.substr(0, i);
}

// Подгоняет строку под ширину, при необходимости добавляя многоточие.
std::string fit_cell(const std::string& text, size_t width) {
  if (utf8_length(text) <= width) {
    return text;
  }
  if (width <= 3) {
    return utf8_truncate(text, width);
  }
  return utf8_truncate(text, width - 3) + "...";
}

// Дополняет строку пробелами справа до нужной ширины.
std::string pad_right_utf8(const std::string& text, size_t width) {
  size_t len = utf8_length(text);
  if (len >= width) {
    return text;
  }
  return text + std::string(width - len, ' ');
}

// Дополняет строку пробелами слева до нужной ширины.
std::string pad_left_utf8(const std::string& text, size_t width) {
  size_t len = utf8_length(text);
  if (len >= width) {
    return text;
  }
  return std::string(width - len, ' ') + text;
}

// Печатает строку таблицы с фиксированными ширинами столбцов.
void print_table_row(const std::vector<std::string>& cols,
                     const std::vector<int>& widths,
                     const std::vector<bool>& align_right) {
  for (size_t i = 0; i < widths.size(); ++i) {
    size_t width = widths[i] < 1 ? 1 : static_cast<size_t>(widths[i]);
    std::string cell = (i < cols.size()) ? fit_cell(cols[i], width) : std::string();
    bool right = i < align_right.size() && align_right[i];
    cell = right ? pad_left_utf8(cell, width) : pad_right_utf8(cell, width);
    std::cout << "| " << cell << " ";
  }
  std::cout << "|\n";
}

// Упрощенный вариант без выравнивания вправо.
void print_table_row(const std::vector<std::string>& cols, const std::vector<int>& widths) {
  print_table_row(cols, widths, {});
}

// Печатает линию-разделитель таблицы.
void print_table_line(const std::vector<int>& widths) {
  for (size_t i = 0; i < widths.size(); ++i) {
    size_t width = widths[i] < 1 ? 1 : static_cast<size_t>(widths[i]);
    std::cout << "+" << std::string(width + 2, '-');
  }
  std::cout << "+\n";
}

// Собирает список оценок в строку.
std::string join_grades(const std::vector<int>& values) {
  std::ostringstream out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << values[i];
  }
  return out.str();
}

double average_from_values(const std::vector<int>& values) {
  if (values.empty()) {
    return -1.0;
  }
  int sum = 0;
  for (int value : values) {
    sum += value;
  }
  return static_cast<double>(sum) / static_cast<double>(values.size());
}

std::vector<int> grades_for_student_subject(const DataStore& data, int student_id, int subject_id) {
  std::vector<Grade> grades;
  for (const auto& grade : data.grades) {
    if (grade.student_id == student_id && grade.subject_id == subject_id) {
      grades.push_back(grade);
    }
  }
  std::sort(grades.begin(), grades.end(),
            [](const Grade& a, const Grade& b) { return a.attempt < b.attempt; });
  std::vector<int> values;
  values.reserve(grades.size());
  for (const auto& grade : grades) {
    values.push_back(grade.value);
  }
  return values;
}

std::map<int, std::vector<int>> grades_by_subject_for_student(const DataStore& data, int student_id) {
  std::map<int, std::vector<Grade>> by_subject;
  for (const auto& grade : data.grades) {
    if (grade.student_id == student_id) {
      by_subject[grade.subject_id].push_back(grade);
    }
  }
  std::map<int, std::vector<int>> result;
  for (auto& entry : by_subject) {
    auto& grades = entry.second;
    std::sort(grades.begin(), grades.end(),
              [](const Grade& a, const Grade& b) { return a.attempt < b.attempt; });
    std::vector<int> values;
    values.reserve(grades.size());
    for (const auto& grade : grades) {
      values.push_back(grade.value);
    }
    result[entry.first] = std::move(values);
  }
  return result;
}

bool matches_group_filter(const Student& student, int group_filter) {
  if (group_filter == 0) {
    return true;
  }
  if (group_filter == -1) {
    return student.group_id == 0;
  }
  return student.group_id == group_filter;
}

std::vector<const Student*> students_for_group_sorted(const DataStore& data, int group_filter) {
  std::vector<const Student*> result;
  for (const auto& student : data.students) {
    if (matches_group_filter(student, group_filter)) {
      result.push_back(&student);
    }
  }
  std::sort(result.begin(), result.end(), [](const Student* a, const Student* b) {
    std::string a_name = to_lower_ascii(a->name);
    std::string b_name = to_lower_ascii(b->name);
    if (a_name != b_name) {
      return a_name < b_name;
    }
    return a->id < b->id;
  });
  return result;
}


// Возвращает имя студента или запасной текст, если не найден.
std::string student_name_or_unknown(const DataStore& data, int id) {
  const Student* student = find_student(data, id);
  return student ? student->name : "Неизвестно";
}

// Возвращает название предмета или запасной текст, если не найден.
std::string subject_name_or_unknown(const DataStore& data, int id) {
  const Subject* subject = find_subject(data, id);
  return subject ? subject->name : "Неизвестно";
}

// Возвращает название группы или текст по умолчанию.
std::string group_name_or_none(const DataStore& data, int id) {
  if (id == 0) {
    return "Без группы";
  }
  const Group* group = find_group(data, id);
  return group ? group->name : "Неизвестная группа";
}

// Запрашивает ID группы, 0 означает "без группы".
int read_group_id_allow_none(const DataStore& data, const std::string& prompt) {
  while (true) {
    int id = read_int(prompt, 0, std::numeric_limits<int>::max());
    if (id == 0) {
      return 0;
    }
    if (find_group(data, id)) {
      return id;
    }
    std::cout << "Группа не найдена.\n";
  }
}

// Запрашивает ID группы, допускает пустой ввод (false, если пусто).
bool read_group_id_optional(const DataStore& data, const std::string& prompt, int& out) {
  while (true) {
    std::string line = trim(read_line(prompt, true));
    if (line.empty()) {
      return false;
    }
    int value = 0;
    if (!parse_int(line, value)) {
      std::cout << "Введите корректное целое число.\n";
      continue;
    }
    if (value < 0) {
      std::cout << "Значение должно быть 0 или больше.\n";
      continue;
    }
    if (value != 0 && !find_group(data, value)) {
      std::cout << "Группа не найдена.\n";
      continue;
    }
    out = value;
    return true;
  }
}

int read_student_id_or_cancel(const DataStore& data, const std::string& prompt) {
  while (true) {
    int id = read_int(prompt, 0, std::numeric_limits<int>::max());
    if (id == 0) {
      return 0;
    }
    if (find_student(data, id)) {
      return id;
    }
    std::cout << "Студент не найден.\n";
  }
}

int read_subject_id_or_cancel(const DataStore& data, const std::string& prompt) {
  while (true) {
    int id = read_int(prompt, 0, std::numeric_limits<int>::max());
    if (id == 0) {
      return 0;
    }
    if (find_subject(data, id)) {
      return id;
    }
    std::cout << "Предмет не найден.\n";
  }
}

// Запрашивает фильтр по группе: 0 - все, -1 - без группы, >0 - конкретная группа.
int read_group_filter(const DataStore& data, const std::string& prompt) {
  while (true) {
    int id = read_int(prompt, -1, std::numeric_limits<int>::max());
    if (id <= 0) {
      return id;
    }
    if (find_group(data, id)) {
      return id;
    }
    std::cout << "Группа не найдена.\n";
  }
}

// Печатает краткий список студентов.
void print_students_simple(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  std::cout << "Студенты:\n";
  const std::vector<int> widths = {4, 28, 20};
  const std::vector<bool> align_right = {true, false, false};
  print_table_line(widths);
  print_table_row({"ID", "ФИО", "Группа"}, widths, align_right);
  print_table_line(widths);
  for (const auto& student : data.students) {
    print_table_row({std::to_string(student.id),
                     student.name,
                     group_name_or_none(data, student.group_id)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Печатает краткий список групп.
void print_groups_simple(const DataStore& data) {
  if (data.groups.empty()) {
    std::cout << "Нет групп.\n";
    return;
  }
  std::cout << "Группы:\n";
  const std::vector<int> widths = {4, 28};
  const std::vector<bool> align_right = {true, false};
  print_table_line(widths);
  print_table_row({"ID", "Название"}, widths, align_right);
  print_table_line(widths);
  for (const auto& group : data.groups) {
    print_table_row({std::to_string(group.id), group.name}, widths, align_right);
  }
  print_table_line(widths);
}

// Печатает краткий список предметов.
void print_subjects_simple(const DataStore& data) {
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  std::cout << "Предметы:\n";
  const std::vector<int> widths = {4, 28};
  const std::vector<bool> align_right = {true, false};
  print_table_line(widths);
  print_table_row({"ID", "Название"}, widths, align_right);
  print_table_line(widths);
  for (const auto& subject : data.subjects) {
    print_table_row({std::to_string(subject.id), subject.name}, widths, align_right);
  }
  print_table_line(widths);
}

// Печатает краткий список оценок.
void print_grades_simple(const DataStore& data) {
  if (data.grades.empty()) {
    std::cout << "Нет оценок.\n";
    return;
  }
  std::cout << "Оценки:\n";
  const std::vector<int> widths = {4, 24, 24, 8, 8};
  const std::vector<bool> align_right = {true, false, false, true, true};
  print_table_line(widths);
  print_table_row({"ID", "Студент", "Предмет", "Попытка", "Оценка"}, widths, align_right);
  print_table_line(widths);
  for (const auto& grade : data.grades) {
    print_table_row({std::to_string(grade.id),
                     student_name_or_unknown(data, grade.student_id),
                     subject_name_or_unknown(data, grade.subject_id),
                     std::to_string(grade.attempt),
                     std::to_string(grade.value)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Печатает подробный список студентов с оценками по предметам.
void list_students_detailed(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  std::cout << "Список студентов:\n";
  const std::vector<int> widths = {4, 28, 20, 12};
  const std::vector<bool> align_right = {true, false, false, true};
  print_table_line(widths);
  print_table_row({"ID", "ФИО", "Группа", "Ср.балл"}, widths, align_right);
  print_table_line(widths);
  for (const auto& student : data.students) {
    auto aggregates = subject_aggregates_for_student(data, student.id);
    double avg = -1.0;
    if (!aggregates.empty()) {
      double sum = 0.0;
      int count = 0;
      for (const auto& entry : aggregates) {
        const SubjectAggregate& agg = entry.second;
        if (agg.count > 0) {
          sum += static_cast<double>(agg.sum) / static_cast<double>(agg.count);
          ++count;
        }
      }
      if (count > 0) {
        avg = sum / static_cast<double>(count);
      }
    }
    print_table_row({std::to_string(student.id),
                     student.name,
                     group_name_or_none(data, student.group_id),
                     format_avg(avg)},
                    widths,
                    align_right);

    if (aggregates.empty()) {
      std::cout << "  Предметы: нет\n";
      continue;
    }
    std::cout << "  Предметы:\n";
    const std::vector<int> subj_widths = {4, 26, 10, 10, 30};
    const std::vector<bool> subj_align = {true, false, true, true, false};
    print_table_line(subj_widths);
    print_table_row({"ID", "Предмет", "Ср.балл", "Последн.", "Оценки"}, subj_widths, subj_align);
    print_table_line(subj_widths);
    for (const auto& entry : aggregates) {
      const SubjectAggregate& agg = entry.second;
      double subj_avg = (agg.count == 0) ? -1.0
                                         : static_cast<double>(agg.sum) / static_cast<double>(agg.count);
      std::vector<Grade> subject_grades;
      subject_grades.reserve(static_cast<size_t>(agg.count));
      for (const auto& grade : data.grades) {
        if (grade.student_id == student.id && grade.subject_id == entry.first) {
          subject_grades.push_back(grade);
        }
      }
      std::sort(subject_grades.begin(), subject_grades.end(),
                [](const Grade& a, const Grade& b) { return a.attempt < b.attempt; });
      std::vector<int> values;
      values.reserve(subject_grades.size());
      for (const auto& grade : subject_grades) {
        values.push_back(grade.value);
      }
      print_table_row({std::to_string(entry.first),
                       subject_name_or_unknown(data, entry.first),
                       format_avg(subj_avg),
                       std::to_string(agg.latest_value),
                       join_grades(values)},
                      subj_widths,
                      subj_align);
    }
    print_table_line(subj_widths);
  }
  print_table_line(widths);
}

struct StudentResult {
  const Student* student = nullptr;
  double avg = -1.0;
};

// Формирует список студентов с учетом фильтров.
std::vector<StudentResult> filter_students(const DataStore& data,
                                           int group_filter,
                                           const std::string& name_query,
                                           bool use_min_avg,
                                           double min_avg) {
  std::vector<StudentResult> results;
  std::string name_query_lower = to_lower_ascii(trim(name_query));
  for (const auto& student : data.students) {
    if (group_filter == -1 && student.group_id != 0) {
      continue;
    }
    if (group_filter > 0 && student.group_id != group_filter) {
      continue;
    }
    if (!name_query_lower.empty()) {
      std::string name_lower = to_lower_ascii(student.name);
      if (name_lower.find(name_query_lower) == std::string::npos) {
        continue;
      }
    }
    double avg = average_subjects_for_student(data, student.id);
    if (use_min_avg) {
      if (avg < 0.0 || avg < min_avg) {
        continue;
      }
    }
    results.push_back({&student, avg});
  }
  return results;
}

// Печатает результат поиска/фильтрации по студентам.
void print_student_results(const DataStore& data, const std::vector<StudentResult>& results) {
  if (results.empty()) {
    std::cout << "Нет подходящих студентов.\n";
    return;
  }
  std::cout << "Результаты (" << results.size() << "):\n";
  const std::vector<int> widths = {4, 28, 20, 12};
  const std::vector<bool> align_right = {true, false, false, true};
  print_table_line(widths);
  print_table_row({"ID", "ФИО", "Группа", "Ср.балл"}, widths, align_right);
  print_table_line(widths);
  for (const auto& item : results) {
    const Student* student = item.student;
    print_table_row({std::to_string(student->id),
                     student->name,
                     group_name_or_none(data, student->group_id),
                     format_avg(item.avg)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Создает запись студента и возвращает его ID.
int create_student_record(DataStore& data, const std::string& name, int group_id) {
  Student student;
  student.id = data.next_student_id++;
  student.name = name;
  student.group_id = group_id;
  data.students.push_back(student);
  return student.id;
}

// Запрашивает группу при создании студента (включая создание новой).
int read_group_for_new_student(DataStore& data) {
  if (data.groups.empty()) {
    int create_group_choice = read_int("Группы отсутствуют. Создать новую? 1-да, 0-нет: ", 0, 1);
    if (create_group_choice == 1) {
      std::string group_name = trim(read_line("Название новой группы: "));
      int group_id = create_group_record(data, group_name);
      std::cout << "Создана группа с ID " << group_id << ".\n";
      return group_id;
    }
    return 0;
  }

  print_groups_simple(data);
  while (true) {
    int group_id = read_int("ID группы (0 - без группы, -1 - создать новую): ",
                            -1, std::numeric_limits<int>::max());
    if (group_id == 0) {
      return 0;
    }
    if (group_id == -1) {
      std::string group_name = trim(read_line("Название новой группы: "));
      int new_group_id = create_group_record(data, group_name);
      std::cout << "Создана группа с ID " << new_group_id << ".\n";
      return new_group_id;
    }
    if (find_group(data, group_id)) {
      return group_id;
    }
    std::cout << "Группа не найдена.\n";
  }
}

// Меню поиска, фильтрации и сортировки студентов.
void students_search_menu(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  if (!data.groups.empty()) {
    print_groups_simple(data);
  }
  int group_filter = read_group_filter(data, "ID группы (0 - все, -1 - без группы): ");
  std::string name_query = read_line("ФИО (часть, пусто - без фильтра): ", true);
  double min_avg = 0.0;
  bool use_min_avg = read_double_optional("Мин. средний балл (пусто - без фильтра): ",
                                          0.0, static_cast<double>(kMaxGrade), min_avg);

  int sort_key = read_int("Сортировка: 1) ID 2) ФИО 3) Средний балл: ", 1, 3);
  int sort_order = read_int("Порядок: 1) Возрастание 2) Убывание: ", 1, 2);
  bool asc = (sort_order == 1);

  std::vector<StudentResult> results =
      filter_students(data, group_filter, name_query, use_min_avg, min_avg);

  auto cmp_asc = [sort_key](const StudentResult& a, const StudentResult& b) {
    if (sort_key == 1) {
      return a.student->id < b.student->id;
    }
    if (sort_key == 2) {
      std::string a_name = to_lower_ascii(a.student->name);
      std::string b_name = to_lower_ascii(b.student->name);
      if (a_name != b_name) {
        return a_name < b_name;
      }
      return a.student->id < b.student->id;
    }
    if (a.avg != b.avg) {
      return a.avg < b.avg;
    }
    return a.student->id < b.student->id;
  };

  std::sort(results.begin(), results.end(),
            [&](const StudentResult& a, const StudentResult& b) {
              return asc ? cmp_asc(a, b) : cmp_asc(b, a);
            });

  print_student_results(data, results);
}

// Добавляет нового студента в список.
void add_student(DataStore& data) {
  std::string name = trim(read_line("Имя студента: "));
  int group_id = read_group_for_new_student(data);
  int student_id = create_student_record(data, name, group_id);
  std::cout << "Добавлен студент с ID " << student_id << ".\n";
  autosave_or_warn(data);
}

// Добавляет студента в выбранную группу.
void add_student_to_group(DataStore& data) {
  if (data.groups.empty()) {
    std::cout << "Сначала добавьте группы.\n";
    return;
  }
  print_groups_simple(data);
  while (true) {
    int group_id = read_int("ID группы для добавления студента: ", 1,
                            std::numeric_limits<int>::max());
    if (!find_group(data, group_id)) {
      std::cout << "Группа не найдена.\n";
      continue;
    }
    std::string name = trim(read_line("Имя студента: "));
    int student_id = create_student_record(data, name, group_id);
    std::cout << "Добавлен студент с ID " << student_id << ".\n";
    autosave_or_warn(data);
    return;
  }
}

// Редактирует данные студента.
void edit_student(DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов для редактирования.\n";
    return;
  }
  print_students_simple(data);
  int id = read_int("ID студента для редактирования: ", 1, std::numeric_limits<int>::max());
  Student* student = find_student(data, id);
  if (!student) {
    std::cout << "Студент не найден.\n";
    return;
  }
  bool changed = false;
  std::string new_name = trim(read_line("Новое имя (пусто - оставить): ", true));
  if (!new_name.empty() && new_name != student->name) {
    student->name = new_name;
    changed = true;
  }
  if (!data.groups.empty()) {
    print_groups_simple(data);
    int new_group_id = 0;
    if (read_group_id_optional(data, "Новый ID группы (пусто - оставить, 0 - без группы): ",
                               new_group_id)) {
      if (new_group_id != student->group_id) {
        student->group_id = new_group_id;
        changed = true;
      }
    }
  }
  std::cout << "Студент обновлен.\n";
  if (changed) {
    autosave_or_warn(data);
  }
}

// Удаляет студента и связанные оценки.
void delete_student(DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов для удаления.\n";
    return;
  }
  print_students_simple(data);
  int id = read_int("ID студента для удаления: ", 1, std::numeric_limits<int>::max());
  auto it = std::find_if(data.students.begin(), data.students.end(),
                         [id](const Student& s) { return s.id == id; });
  if (it == data.students.end()) {
    std::cout << "Студент не найден.\n";
    return;
  }
  data.students.erase(it);
  // Удаляем все оценки, связанные с этим студентом.
  size_t before = data.grades.size();
  data.grades.erase(
      std::remove_if(data.grades.begin(), data.grades.end(),
                     [id](const Grade& g) { return g.student_id == id; }),
      data.grades.end());
  size_t removed = before - data.grades.size();
  std::cout << "Студент удален. Удалено связанных оценок: " << removed << ".\n";
  autosave_or_warn(data);
}

// Создает запись группы и возвращает ее ID.
int create_group_record(DataStore& data, const std::string& name) {
  Group group;
  group.id = data.next_group_id++;
  group.name = name;
  data.groups.push_back(group);
  return group.id;
}

// Добавляет новую группу.
void add_group(DataStore& data) {
  std::string name = trim(read_line("Название группы: "));
  int group_id = create_group_record(data, name);
  std::cout << "Добавлена группа с ID " << group_id << ".\n";
  autosave_or_warn(data);
}

// Редактирует данные группы.
void edit_group(DataStore& data) {
  if (data.groups.empty()) {
    std::cout << "Нет групп для редактирования.\n";
    return;
  }
  print_groups_simple(data);
  int id = read_int("ID группы для редактирования: ", 1, std::numeric_limits<int>::max());
  Group* group = find_group(data, id);
  if (!group) {
    std::cout << "Группа не найдена.\n";
    return;
  }
  bool changed = false;
  std::string new_name = trim(read_line("Новое название (пусто - оставить): ", true));
  if (!new_name.empty() && new_name != group->name) {
    group->name = new_name;
    changed = true;
  }
  std::cout << "Группа обновлена.\n";
  if (changed) {
    autosave_or_warn(data);
  }
}

// Удаляет группу и снимает привязку у студентов.
void delete_group(DataStore& data) {
  if (data.groups.empty()) {
    std::cout << "Нет групп для удаления.\n";
    return;
  }
  print_groups_simple(data);
  int id = read_int("ID группы для удаления: ", 1, std::numeric_limits<int>::max());
  auto it = std::find_if(data.groups.begin(), data.groups.end(),
                         [id](const Group& g) { return g.id == id; });
  if (it == data.groups.end()) {
    std::cout << "Группа не найдена.\n";
    return;
  }
  data.groups.erase(it);
  int updated = 0;
  for (auto& student : data.students) {
    if (student.group_id == id) {
      student.group_id = 0;
      ++updated;
    }
  }
  std::cout << "Группа удалена. Студентов обновлено: " << updated << ".\n";
  autosave_or_warn(data);
}

// Добавляет новый предмет.
void add_subject(DataStore& data) {
  std::string name = trim(read_line("Название предмета: "));
  Subject subject;
  subject.id = data.next_subject_id++;
  subject.name = name;
  data.subjects.push_back(subject);
  std::cout << "Добавлен предмет с ID " << subject.id << ".\n";
  autosave_or_warn(data);
}

// Редактирует данные предмета.
void edit_subject(DataStore& data) {
  if (data.subjects.empty()) {
    std::cout << "Нет предметов для редактирования.\n";
    return;
  }
  print_subjects_simple(data);
  int id = read_int("ID предмета для редактирования: ", 1, std::numeric_limits<int>::max());
  Subject* subject = find_subject(data, id);
  if (!subject) {
    std::cout << "Предмет не найден.\n";
    return;
  }
  bool changed = false;
  std::string new_name = trim(read_line("Новое название (пусто - оставить): ", true));
  if (!new_name.empty() && new_name != subject->name) {
    subject->name = new_name;
    changed = true;
  }
  std::cout << "Предмет обновлен.\n";
  if (changed) {
    autosave_or_warn(data);
  }
}

// Удаляет предмет и связанные оценки.
void delete_subject(DataStore& data) {
  if (data.subjects.empty()) {
    std::cout << "Нет предметов для удаления.\n";
    return;
  }
  print_subjects_simple(data);
  int id = read_int("ID предмета для удаления: ", 1, std::numeric_limits<int>::max());
  auto it = std::find_if(data.subjects.begin(), data.subjects.end(),
                         [id](const Subject& s) { return s.id == id; });
  if (it == data.subjects.end()) {
    std::cout << "Предмет не найден.\n";
    return;
  }
  data.subjects.erase(it);
  // Удаляем все оценки, связанные с этим предметом.
  size_t before = data.grades.size();
  data.grades.erase(
      std::remove_if(data.grades.begin(), data.grades.end(),
                     [id](const Grade& g) { return g.subject_id == id; }),
      data.grades.end());
  size_t removed = before - data.grades.size();
  std::cout << "Предмет удален. Удалено связанных оценок: " << removed << ".\n";
  autosave_or_warn(data);
}

// Добавляет оценку студенту по предмету.
void add_grade(DataStore& data) {
  if (data.students.empty()) {
    int choice = read_int("Студентов нет. Создать сейчас? 1-да, 0-нет: ", 0, 1);
    if (choice == 1) {
      add_student(data);
    }
    if (data.students.empty()) {
      std::cout << "Сначала добавьте студентов.\n";
      return;
    }
  }
  if (data.subjects.empty()) {
    int choice = read_int("Предметов нет. Создать сейчас? 1-да, 0-нет: ", 0, 1);
    if (choice == 1) {
      add_subject(data);
    }
    if (data.subjects.empty()) {
      std::cout << "Сначала добавьте предметы.\n";
      return;
    }
  }
  print_students_simple(data);
  int student_id = read_student_id_or_cancel(data, "ID студента (0 - отмена): ");
  if (student_id == 0) {
    std::cout << "Операция отменена.\n";
    return;
  }
  print_subjects_simple(data);
  int subject_id = read_subject_id_or_cancel(data, "ID предмета (0 - отмена): ");
  if (subject_id == 0) {
    std::cout << "Операция отменена.\n";
    return;
  }
  int value = read_int("Оценка (1-5): ", kMinGrade, kMaxGrade);
  Grade grade;
  grade.id = data.next_grade_id++;
  grade.student_id = student_id;
  grade.subject_id = subject_id;
  grade.value = value;
  // Номер попытки зависит от количества прошлых оценок по предмету.
  grade.attempt = next_attempt(data, student_id, subject_id);
  data.grades.push_back(grade);
  std::cout << "Добавлена оценка с ID " << grade.id << " (попытка "
            << grade.attempt << ").\n";
  autosave_or_warn(data);
}


// Редактирует значение оценки.
void edit_grade(DataStore& data) {
  if (data.grades.empty()) {
    std::cout << "Нет оценок для редактирования.\n";
    return;
  }
  print_grades_simple(data);
  int id = read_int("ID оценки для редактирования: ", 1, std::numeric_limits<int>::max());
  Grade* grade = find_grade(data, id);
  if (!grade) {
    std::cout << "Оценка не найдена.\n";
    return;
  }
  bool changed = false;
  int new_value = 0;
  if (read_int_optional("Новая оценка (1-5, пусто - оставить): ", kMinGrade, kMaxGrade, new_value)) {
    if (new_value != grade->value) {
      grade->value = new_value;
      changed = true;
    }
  }
  std::cout << "Оценка обновлена.\n";
  if (changed) {
    autosave_or_warn(data);
  }
}

// Удаляет оценку по ID.
void delete_grade(DataStore& data) {
  if (data.grades.empty()) {
    std::cout << "Нет оценок для удаления.\n";
    return;
  }
  print_grades_simple(data);
  int id = read_int("ID оценки для удаления: ", 1, std::numeric_limits<int>::max());
  auto it = std::find_if(data.grades.begin(), data.grades.end(),
                         [id](const Grade& g) { return g.id == id; });
  if (it == data.grades.end()) {
    std::cout << "Оценка не найдена.\n";
    return;
  }
  data.grades.erase(it);
  std::cout << "Оценка удалена.\n";
  autosave_or_warn(data);
}

// Отчет: средние баллы по студентам и общий средний.
void report_overall_averages(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  std::cout << "Средние по студентам (все оценки по предметам):\n";
  const std::vector<int> widths = {4, 28, 20, 12};
  const std::vector<bool> align_right = {true, false, false, true};
  print_table_line(widths);
  print_table_row({"ID", "ФИО", "Группа", "Ср.балл"}, widths, align_right);
  print_table_line(widths);
  double total = 0.0;
  int count = 0;
  for (const auto& student : data.students) {
    double avg = average_subjects_for_student(data, student.id);
    print_table_row({std::to_string(student.id),
                     student.name,
                     group_name_or_none(data, student.group_id),
                     format_avg(avg)},
                    widths,
                    align_right);
    if (avg >= 0.0) {
      total += avg;
      ++count;
    }
  }
  print_table_line(widths);
  if (count > 0) {
    std::cout << "Общий средний балл: " << format_avg(total / static_cast<double>(count)) << "\n";
  } else {
    std::cout << "Общий средний балл: нет\n";
  }
}

// Отчет: средние баллы по предметам.
void report_subject_averages(const DataStore& data) {
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  std::cout << "Средние по предметам (все оценки):\n";
  const std::vector<int> widths = {4, 28, 12, 10};
  const std::vector<bool> align_right = {true, false, true, true};
  print_table_line(widths);
  print_table_row({"ID", "Предмет", "Ср.балл", "Оценок"}, widths, align_right);
  print_table_line(widths);
  for (const auto& subject : data.subjects) {
    int count = 0;
    double avg = average_all_for_subject(data, subject.id, &count);
    print_table_row({std::to_string(subject.id),
                     subject.name,
                     format_avg(avg),
                     std::to_string(count)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Отчет: подробности по выбранному предмету.
void report_subject_detail(const DataStore& data) {
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  print_subjects_simple(data);
  int subject_id = read_int("ID предмета для подробностей: ", 1, std::numeric_limits<int>::max());
  const Subject* subject = find_subject(data, subject_id);
  if (!subject) {
    std::cout << "Предмет не найден.\n";
    return;
  }
  // Группируем оценки по студентам для выбранного предмета.
  std::map<int, std::vector<Grade>> by_student;
  for (const auto& grade : data.grades) {
    if (grade.subject_id == subject_id) {
      by_student[grade.student_id].push_back(grade);
    }
  }
  if (by_student.empty()) {
    std::cout << "Нет оценок по предмету " << subject->name << ".\n";
    return;
  }
  std::cout << "Подробности по предмету: " << subject->name << "\n";
  const std::vector<int> widths = {28, 10, 10, 36};
  const std::vector<bool> align_right = {false, true, true, false};
  print_table_line(widths);
  print_table_row({"Студент", "Ср.балл", "Последн.", "Оценки"}, widths, align_right);
  print_table_line(widths);
  for (const auto& entry : by_student) {
    const Student* student = find_student(data, entry.first);
    std::string student_name = student ? student->name : "Неизвестно";
    std::vector<Grade> grades = entry.second;
    // Сортируем попытки по порядку сдачи.
    std::sort(grades.begin(), grades.end(),
              [](const Grade& a, const Grade& b) { return a.attempt < b.attempt; });
    int sum = 0;
    int latest_value = grades.back().value;
    std::vector<int> values;
    values.reserve(grades.size());
    for (const auto& grade : grades) {
      sum += grade.value;
      values.push_back(grade.value);
    }
    double avg = static_cast<double>(sum) / static_cast<double>(grades.size());
    print_table_row({student_name,
                     format_avg(avg),
                     std::to_string(latest_value),
                     join_grades(values)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Отчет: топ-N студентов по среднему баллу.
void report_top_n(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  struct Entry {
    int student_id = 0;
    double avg = -1.0;
  };
  std::vector<Entry> entries;
  for (const auto& student : data.students) {
    double avg = average_subjects_for_student(data, student.id);
    if (avg >= 0.0) {
      entries.push_back({student.id, avg});
    }
  }
  if (entries.empty()) {
    std::cout << "Нет оценок.\n";
    return;
  }
  // Сортируем по среднему баллу по убыванию, затем по ID.
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) {
              if (a.avg != b.avg) {
                return a.avg > b.avg;
              }
              return a.student_id < b.student_id;
            });
  int max_n = static_cast<int>(entries.size());
  int n = read_int("Топ N (1.." + std::to_string(max_n) + "): ", 1, max_n);
  std::cout << "Топ " << n << " студентов:\n";
  const std::vector<int> widths = {3, 28, 20, 12};
  const std::vector<bool> align_right = {true, false, false, true};
  print_table_line(widths);
  print_table_row({"#", "ФИО", "Группа", "Ср.балл"}, widths, align_right);
  print_table_line(widths);
  for (int i = 0; i < n; ++i) {
    const Entry& entry = entries[i];
    const Student* student = find_student(data, entry.student_id);
    std::string name = student ? student->name : "Неизвестно";
    std::string group_name = student ? group_name_or_none(data, student->group_id) : "Неизвестно";
    print_table_row({std::to_string(i + 1), name, group_name, format_avg(entry.avg)},
                    widths,
                    align_right);
  }
  print_table_line(widths);
}

// Отчет: список пересдач по последним оценкам.
void report_retakes(const DataStore& data) {
  if (data.students.empty() || data.subjects.empty()) {
    std::cout << "Нет студентов или предметов.\n";
    return;
  }
  std::cout << "Пересдачи (последняя оценка < " << kPassGrade << "):\n";
  std::vector<std::vector<std::string>> rows;
  for (const auto& student : data.students) {
    // Анализируем только последнюю оценку по каждому предмету.
    auto aggregates = subject_aggregates_for_student(data, student.id);
    for (const auto& entry : aggregates) {
      if (entry.second.latest_value < kPassGrade) {
        rows.push_back({student.name,
                        subject_name_or_unknown(data, entry.first),
                        std::to_string(entry.second.latest_value)});
      }
    }
  }
  if (rows.empty()) {
    std::cout << "  Нет.\n";
    return;
  }
  const std::vector<int> widths = {28, 28, 10};
  const std::vector<bool> align_right = {false, false, true};
  print_table_line(widths);
  print_table_row({"Студент", "Предмет", "Оценка"}, widths, align_right);
  print_table_line(widths);
  for (const auto& row : rows) {
    print_table_row(row, widths, align_right);
  }
  print_table_line(widths);
}

void journal_matrix(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  int group_filter = 0;
  if (!data.groups.empty()) {
    print_groups_simple(data);
    group_filter = read_group_filter(data, "ID группы (0 - все, -1 - без группы): ");
  }
  auto students = students_for_group_sorted(data, group_filter);
  if (students.empty()) {
    std::cout << "Нет студентов для выбранного фильтра.\n";
    return;
  }
  std::cout << "Электронный журнал (последние оценки):\n";
  if (group_filter == -1) {
    std::cout << "Группа: без группы\n";
  } else if (group_filter > 0) {
    std::cout << "Группа: " << group_name_or_none(data, group_filter) << "\n";
  }

  std::vector<int> widths = {4, 24, 18};
  std::vector<bool> align_right = {true, false, false};
  std::vector<std::string> header = {"ID", "ФИО", "Группа"};
  for (const auto& subject : data.subjects) {
    widths.push_back(8);
    align_right.push_back(true);
    header.push_back(subject.name);
  }
  widths.push_back(10);
  align_right.push_back(true);
  header.push_back("Ср.балл");

  print_table_line(widths);
  print_table_row(header, widths, align_right);
  print_table_line(widths);
  for (const auto* student : students) {
    auto by_subject = grades_by_subject_for_student(data, student->id);
    std::vector<std::string> row;
    row.reserve(header.size());
    row.push_back(std::to_string(student->id));
    row.push_back(student->name);
    row.push_back(group_name_or_none(data, student->group_id));
    for (const auto& subject : data.subjects) {
      auto it = by_subject.find(subject.id);
      if (it == by_subject.end() || it->second.empty()) {
        row.push_back("-");
      } else {
        row.push_back(std::to_string(it->second.back()));
      }
    }
    row.push_back(format_avg(average_subjects_for_student(data, student->id)));
    print_table_row(row, widths, align_right);
  }
  print_table_line(widths);
}

void journal_by_subject(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  print_subjects_simple(data);
  int subject_id = read_subject_id_or_cancel(data, "ID предмета (0 - отмена): ");
  if (subject_id == 0) {
    std::cout << "Операция отменена.\n";
    return;
  }
  const Subject* subject = find_subject(data, subject_id);
  if (!subject) {
    std::cout << "Предмет не найден.\n";
    return;
  }
  int group_filter = 0;
  if (!data.groups.empty()) {
    print_groups_simple(data);
    group_filter = read_group_filter(data, "ID группы (0 - все, -1 - без группы): ");
  }
  auto students = students_for_group_sorted(data, group_filter);
  if (students.empty()) {
    std::cout << "Нет студентов для выбранного фильтра.\n";
    return;
  }
  std::cout << "Электронный журнал по предмету: " << subject->name << "\n";
  if (group_filter == -1) {
    std::cout << "Группа: без группы\n";
  } else if (group_filter > 0) {
    std::cout << "Группа: " << group_name_or_none(data, group_filter) << "\n";
  }

  const std::vector<int> widths = {4, 24, 18, 24, 10, 10, 8};
  const std::vector<bool> align_right = {true, false, false, false, true, true, true};
  print_table_line(widths);
  print_table_row({"ID", "ФИО", "Группа", "Оценки", "Ср.балл", "Последн.", "Попыток"},
                  widths, align_right);
  print_table_line(widths);
  for (const auto* student : students) {
    std::vector<int> values = grades_for_student_subject(data, student->id, subject_id);
    std::string grades_text = values.empty() ? "нет" : join_grades(values);
    std::string latest_text = values.empty() ? "нет" : std::to_string(values.back());
    print_table_row({std::to_string(student->id),
                     student->name,
                     group_name_or_none(data, student->group_id),
                     grades_text,
                     format_avg(average_from_values(values)),
                     latest_text,
                     std::to_string(values.size())},
                    widths, align_right);
  }
  print_table_line(widths);
}

void journal_by_student(const DataStore& data) {
  if (data.students.empty()) {
    std::cout << "Нет студентов.\n";
    return;
  }
  print_students_simple(data);
  int student_id = read_student_id_or_cancel(data, "ID студента (0 - отмена): ");
  if (student_id == 0) {
    std::cout << "Операция отменена.\n";
    return;
  }
  const Student* student = find_student(data, student_id);
  if (!student) {
    std::cout << "Студент не найден.\n";
    return;
  }
  if (data.subjects.empty()) {
    std::cout << "Нет предметов.\n";
    return;
  }
  std::cout << "Электронный журнал студента: " << student->name << "\n";
  std::cout << "Группа: " << group_name_or_none(data, student->group_id) << "\n";

  const std::vector<int> widths = {4, 26, 24, 10, 10, 8};
  const std::vector<bool> align_right = {true, false, false, true, true, true};
  print_table_line(widths);
  print_table_row({"ID", "Предмет", "Оценки", "Ср.балл", "Последн.", "Попыток"},
                  widths, align_right);
  print_table_line(widths);
  for (const auto& subject : data.subjects) {
    std::vector<int> values = grades_for_student_subject(data, student->id, subject.id);
    std::string grades_text = values.empty() ? "нет" : join_grades(values);
    std::string latest_text = values.empty() ? "нет" : std::to_string(values.back());
    print_table_row({std::to_string(subject.id),
                     subject.name,
                     grades_text,
                     format_avg(average_from_values(values)),
                     latest_text,
                     std::to_string(values.size())},
                    widths, align_right);
  }
  print_table_line(widths);
  std::cout << "Средний балл по предметам: "
            << format_avg(average_subjects_for_student(data, student->id)) << "\n";
}

void journal_menu(const DataStore& data) {
  while (true) {
    std::cout << "\n[Электронный журнал]\n"
              << "1) Сводный журнал (последние оценки)\n"
              << "2) Журнал по предмету (все попытки)\n"
              << "3) Журнал по студенту (все попытки)\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 3);
    switch (choice) {
      case 1:
        journal_matrix(data);
        break;
      case 2:
        journal_by_subject(data);
        break;
      case 3:
        journal_by_student(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Выполняет SQL без возвращаемых строк.
bool exec_sql(sqlite3* db, const std::string& sql) {
  char* err = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (err) {
      std::cout << "Ошибка SQLite: " << err << "\n";
      sqlite3_free(err);
    }
    return false;
  }
  return true;
}

// Создает таблицы, если они еще не созданы.
bool init_db(sqlite3* db) {
  const char* sql =
      "PRAGMA foreign_keys = ON;"
      "CREATE TABLE IF NOT EXISTS groups ("
      "  id INTEGER PRIMARY KEY,"
      "  name TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS students ("
      "  id INTEGER PRIMARY KEY,"
      "  name TEXT NOT NULL,"
      "  group_id INTEGER,"
      "  FOREIGN KEY(group_id) REFERENCES groups(id)"
      ");"
      "CREATE TABLE IF NOT EXISTS subjects ("
      "  id INTEGER PRIMARY KEY,"
      "  name TEXT NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS grades ("
      "  id INTEGER PRIMARY KEY,"
      "  student_id INTEGER NOT NULL,"
      "  subject_id INTEGER NOT NULL,"
      "  value INTEGER NOT NULL,"
      "  attempt INTEGER NOT NULL,"
      "  FOREIGN KEY(student_id) REFERENCES students(id),"
      "  FOREIGN KEY(subject_id) REFERENCES subjects(id)"
      ");";
  return exec_sql(db, sql);
}

// Безопасно читает текстовую колонку.
std::string column_text(sqlite3_stmt* stmt, int col) {
  const unsigned char* text = sqlite3_column_text(stmt, col);
  if (!text) {
    return std::string();
  }
  return std::string(reinterpret_cast<const char*>(text));
}

// Экранирует значение для CSV с учетом разделителя.
std::string csv_escape(const std::string& text, char delim) {
  bool needs_quotes = false;
  for (char c : text) {
    if (c == delim || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return text;
  }
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (char c : text) {
    if (c == '"') {
      out.push_back('"');
      out.push_back('"');
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

// Автосохранение после изменений.
void autosave_or_warn(const DataStore& data) {
  if (!save_data(data, db_path())) {
    std::cout << "Автосохранение не удалось.\n";
  }
}

// Сохраняет все данные в SQLite для восстановления при следующем запуске.
bool save_data(const DataStore& data, const std::string& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    if (db) {
      sqlite3_close(db);
    }
    return false;
  }
  if (!init_db(db)) {
    sqlite3_close(db);
    return false;
  }
  bool ok = exec_sql(db, "BEGIN IMMEDIATE;");
  if (!ok) {
    sqlite3_close(db);
    return false;
  }
  ok = exec_sql(db, "DELETE FROM grades; DELETE FROM students; DELETE FROM subjects; DELETE FROM groups;");
  if (!ok) {
    exec_sql(db, "ROLLBACK;");
    sqlite3_close(db);
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  ok = sqlite3_prepare_v2(db, "INSERT INTO groups(id, name) VALUES(?, ?);", -1, &stmt, nullptr) == SQLITE_OK;
  if (ok) {
    for (const auto& group : data.groups) {
      sqlite3_bind_int(stmt, 1, group.id);
      sqlite3_bind_text(stmt, 2, group.name.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = false;
        break;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (ok) {
    ok = sqlite3_prepare_v2(db, "INSERT INTO students(id, name, group_id) VALUES(?, ?, ?);",
                            -1, &stmt, nullptr) == SQLITE_OK;
  }
  if (ok) {
    for (const auto& student : data.students) {
      sqlite3_bind_int(stmt, 1, student.id);
      sqlite3_bind_text(stmt, 2, student.name.c_str(), -1, SQLITE_TRANSIENT);
      if (student.group_id == 0) {
        sqlite3_bind_null(stmt, 3);
      } else {
        sqlite3_bind_int(stmt, 3, student.group_id);
      }
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = false;
        break;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (ok) {
    ok = sqlite3_prepare_v2(db, "INSERT INTO subjects(id, name) VALUES(?, ?);",
                            -1, &stmt, nullptr) == SQLITE_OK;
  }
  if (ok) {
    for (const auto& subject : data.subjects) {
      sqlite3_bind_int(stmt, 1, subject.id);
      sqlite3_bind_text(stmt, 2, subject.name.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = false;
        break;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (ok) {
    ok = sqlite3_prepare_v2(db,
                            "INSERT INTO grades(id, student_id, subject_id, value, attempt) "
                            "VALUES(?, ?, ?, ?, ?);",
                            -1, &stmt, nullptr) == SQLITE_OK;
  }
  if (ok) {
    for (const auto& grade : data.grades) {
      sqlite3_bind_int(stmt, 1, grade.id);
      sqlite3_bind_int(stmt, 2, grade.student_id);
      sqlite3_bind_int(stmt, 3, grade.subject_id);
      sqlite3_bind_int(stmt, 4, grade.value);
      sqlite3_bind_int(stmt, 5, grade.attempt);
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        ok = false;
        break;
      }
      sqlite3_reset(stmt);
      sqlite3_clear_bindings(stmt);
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  if (ok) {
    ok = exec_sql(db, "COMMIT;");
  } else {
    exec_sql(db, "ROLLBACK;");
  }
  sqlite3_close(db);
  return ok;
}

// Загружает данные из SQLite; возвращает true, если файл существовал.
bool load_data(DataStore& data, const std::string& path) {
  bool existed = static_cast<bool>(std::ifstream(path));
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    if (db) {
      sqlite3_close(db);
    }
    return false;
  }
  if (!init_db(db)) {
    sqlite3_close(db);
    return false;
  }

  DataStore temp;
  sqlite3_stmt* stmt = nullptr;

  if (sqlite3_prepare_v2(db, "SELECT id, name FROM groups ORDER BY id;", -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int id = sqlite3_column_int(stmt, 0);
      std::string name = column_text(stmt, 1);
      temp.groups.push_back({id, name});
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id, name, group_id FROM students ORDER BY id;", -1, &stmt, nullptr) ==
      SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int id = sqlite3_column_int(stmt, 0);
      std::string name = column_text(stmt, 1);
      int group_id = 0;
      if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
        group_id = sqlite3_column_int(stmt, 2);
      }
      temp.students.push_back({id, name, group_id});
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id, name FROM subjects ORDER BY id;", -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int id = sqlite3_column_int(stmt, 0);
      std::string name = column_text(stmt, 1);
      temp.subjects.push_back({id, name});
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }

  stmt = nullptr;
  if (sqlite3_prepare_v2(
          db, "SELECT id, student_id, subject_id, value, attempt FROM grades ORDER BY id;", -1, &stmt, nullptr) ==
      SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Grade grade;
      grade.id = sqlite3_column_int(stmt, 0);
      grade.student_id = sqlite3_column_int(stmt, 1);
      grade.subject_id = sqlite3_column_int(stmt, 2);
      grade.value = sqlite3_column_int(stmt, 3);
      grade.attempt = sqlite3_column_int(stmt, 4);
      temp.grades.push_back(grade);
    }
  }
  if (stmt) {
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);

  std::set<int> group_ids;
  for (const auto& group : temp.groups) {
    group_ids.insert(group.id);
  }
  for (auto& student : temp.students) {
    if (student.group_id != 0 && group_ids.find(student.group_id) == group_ids.end()) {
      student.group_id = 0;
    }
  }

  std::set<int> student_ids;
  for (const auto& student : temp.students) {
    student_ids.insert(student.id);
  }
  std::set<int> subject_ids;
  for (const auto& subject : temp.subjects) {
    subject_ids.insert(subject.id);
  }
  temp.grades.erase(
      std::remove_if(temp.grades.begin(), temp.grades.end(),
                     [&](const Grade& g) {
                       return student_ids.find(g.student_id) == student_ids.end() ||
                              subject_ids.find(g.subject_id) == subject_ids.end();
                     }),
      temp.grades.end());

  auto next_id_from = [](int start, const auto& items) {
    int max_id = 0;
    for (const auto& item : items) {
      if (item.id > max_id) {
        max_id = item.id;
      }
    }
    return std::max(start, max_id + 1);
  };

  temp.next_student_id = next_id_from(1, temp.students);
  temp.next_subject_id = next_id_from(1, temp.subjects);
  temp.next_group_id = next_id_from(1, temp.groups);
  temp.next_grade_id = next_id_from(1, temp.grades);

  data = std::move(temp);
  return existed;
}

// Экспортирует данные в CSV-файлы для открытия в Excel.
void export_csv(const DataStore& data) {
  ensure_storage_dirs();
  std::ofstream groups_file(export_path("export_groups.csv"), std::ios::binary);
  std::ofstream students_file(export_path("export_students.csv"), std::ios::binary);
  std::ofstream subjects_file(export_path("export_subjects.csv"), std::ios::binary);
  std::ofstream grades_file(export_path("export_grades.csv"), std::ios::binary);
  if (!groups_file || !students_file || !subjects_file || !grades_file) {
    std::cout << "Не удалось открыть файлы для экспорта.\n";
    return;
  }

  auto write_utf8_bom = [](std::ofstream& out) {
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    out.write(reinterpret_cast<const char*>(bom), sizeof(bom));
  };
  write_utf8_bom(groups_file);
  write_utf8_bom(students_file);
  write_utf8_bom(subjects_file);
  write_utf8_bom(grades_file);


  groups_file << "ID_группы" << kCsvDelim << "Название_группы\n";
  for (const auto& group : data.groups) {
    groups_file << group.id << kCsvDelim
                << csv_escape(group.name, kCsvDelim) << "\n";
  }

  // Используем точку с запятой - привычный разделитель для Excel в RU локали.
  students_file << "ID_студента" << kCsvDelim << "Имя_студента" << kCsvDelim
                << "ID_группы" << kCsvDelim << "Группа\n";
  for (const auto& student : data.students) {
    students_file << student.id << kCsvDelim
                  << csv_escape(student.name, kCsvDelim) << kCsvDelim
                  << student.group_id << kCsvDelim
                  << csv_escape(group_name_or_none(data, student.group_id), kCsvDelim) << "\n";
  }

  subjects_file << "ID_предмета" << kCsvDelim << "Название_предмета\n";
  for (const auto& subject : data.subjects) {
    subjects_file << subject.id << kCsvDelim
                  << csv_escape(subject.name, kCsvDelim) << "\n";
  }

  grades_file << "ID_оценки" << kCsvDelim << "ID_студента" << kCsvDelim
              << "ID_предмета" << kCsvDelim << "Попытка" << kCsvDelim
              << "Оценка\n";
  for (const auto& grade : data.grades) {
    grades_file << grade.id << kCsvDelim
                << grade.student_id << kCsvDelim
                << grade.subject_id << kCsvDelim
                << grade.attempt << kCsvDelim
                << grade.value << "\n";
  }

  std::cout << "Экспортировано в папку '" << kExportDir << "': "
               "export_groups.csv, export_students.csv, "
               "export_subjects.csv, export_grades.csv\n";
}

// Подменю управления студентами.
void students_menu(DataStore& data) {
  while (true) {
    std::cout << "\n[Студенты]\n"
              << "1) Добавить студента\n"
              << "2) Редактировать студента\n"
              << "3) Удалить студента\n"
              << "4) Список студентов\n"
              << "5) Поиск, фильтры и сортировка\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 5);
    switch (choice) {
      case 1:
        add_student(data);
        break;
      case 2:
        edit_student(data);
        break;
      case 3:
        delete_student(data);
        break;
      case 4:
        list_students_detailed(data);
        break;
      case 5:
        students_search_menu(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Подменю управления группами.
void groups_menu(DataStore& data) {
  while (true) {
    std::cout << "\n[Группы]\n"
              << "1) Добавить группу\n"
              << "2) Редактировать группу\n"
              << "3) Удалить группу\n"
              << "4) Список групп\n"
              << "5) Добавить студента в группу\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 5);
    switch (choice) {
      case 1:
        add_group(data);
        break;
      case 2:
        edit_group(data);
        break;
      case 3:
        delete_group(data);
        break;
      case 4:
        print_groups_simple(data);
        break;
      case 5:
        add_student_to_group(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Подменю управления предметами.
void subjects_menu(DataStore& data) {
  while (true) {
    std::cout << "\n[Предметы]\n"
              << "1) Добавить предмет\n"
              << "2) Редактировать предмет\n"
              << "3) Удалить предмет\n"
              << "4) Список предметов\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 4);
    switch (choice) {
      case 1:
        add_subject(data);
        break;
      case 2:
        edit_subject(data);
        break;
      case 3:
        delete_subject(data);
        break;
      case 4:
        print_subjects_simple(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Подменю управления оценками.
void grades_menu(DataStore& data) {
  while (true) {
    std::cout << "\n[Оценки]\n"
              << "1) Добавить оценку\n"
              << "2) Редактировать оценку\n"
              << "3) Удалить оценку\n"
              << "4) Список оценок\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 4);
    switch (choice) {
      case 1:
        add_grade(data);
        break;
      case 2:
        edit_grade(data);
        break;
      case 3:
        delete_grade(data);
        break;
      case 4:
        print_grades_simple(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Подменю отчетов.
void reports_menu(DataStore& data) {
  while (true) {
    std::cout << "\n[Отчеты]\n"
              << "1) Средние по студентам\n"
              << "2) Средние по предметам\n"
              << "3) Подробности по предмету\n"
              << "4) Топ-N студентов\n"
              << "5) Пересдачи\n"
              << "0) Назад\n";
    int choice = read_int("Выберите: ", 0, 5);
    switch (choice) {
      case 1:
        report_overall_averages(data);
        break;
      case 2:
        report_subject_averages(data);
        break;
      case 3:
        report_subject_detail(data);
        break;
      case 4:
        report_top_n(data);
        break;
      case 5:
        report_retakes(data);
        break;
      case 0:
        return;
      default:
        break;
    }
  }
}

// Точка входа: главное меню приложения.
int main() {
  DataStore data;
#ifdef _WIN32
  // Переключаем консоль на UTF-8, чтобы корректно отображать кириллицу.
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  ensure_storage_dirs();
  if (load_data(data, db_path())) {
    std::cout << "Данные загружены из " << db_path() << ".\n";
  }
  while (true) {
    std::cout << "\n[Главное меню]\n"
              << "1) Студенты\n"
              << "2) Группы\n"
              << "3) Предметы\n"
              << "4) Оценки\n"
              << "5) Отчеты\n"
              << "6) Электронный журнал\n"
              << "7) Экспорт в CSV (Excel)\n"
              << "0) Выход\n";
    int choice = read_int("Выберите: ", 0, 7);
    switch (choice) {
      case 1:
        students_menu(data);
        break;
      case 2:
        groups_menu(data);
        break;
      case 3:
        subjects_menu(data);
        break;
      case 4:
        grades_menu(data);
        break;
      case 5:
        reports_menu(data);
        break;
      case 6:
        journal_menu(data);
        break;
      case 7:
        export_csv(data);
        break;
      case 0:
        if (save_data(data, db_path())) {
          std::cout << "Данные сохранены.\n";
        } else {
          std::cout << "Не удалось сохранить данные.\n";
        }
        std::cout << "До свидания.\n";
        return 0;
      default:
        break;
    }
  }
}
