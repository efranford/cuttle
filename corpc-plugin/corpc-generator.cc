/*
 * corpc-generator.cc
 *
 *  Created on: Sep 26, 2016
 *      Author: amyznikov
 */
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/compiler/command_line_interface.h>
#include <google/protobuf/compiler/code_generator.h>
#include <google/protobuf/descriptor.pb.h>
#include <string>
#include <vector>
#include <stdlib.h>

using namespace std;
using namespace google;
using namespace google::protobuf;
using namespace google::protobuf::io;
using namespace google::protobuf::compiler;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class C_Generator
{
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(C_Generator);

  struct c_options {
    bool do_pkgprefix;
    c_options() : do_pkgprefix(true)
      {}
  } options;

public:
  C_Generator()
  {
  }

  ~C_Generator()
  {
  }

  bool Generate(const FileDescriptor * file, const string & opts, GeneratorContext * gctx, string * status)
  {
    fprintf(stderr, "Generate() started\n");

    if ( !parse_options(opts, &options, status) ) {
      return false;
    }

    const string basename = strip_proto_suffix(file->name()) + ".pb-c";

    // Generate header.
    {
      scoped_ptr<io::ZeroCopyOutputStream> output(gctx->Open(basename + ".h"));
      Printer printer(output.get(), '$');
      generate_c_header(file, &printer);
    }

    // Generate cc file.
    {
      scoped_ptr<io::ZeroCopyOutputStream> output(gctx->Open(basename + ".c"));
      Printer printer(output.get(), '$');
      generate_c_source(file, &printer);
    }

    return true;
  }


  static bool parse_options(const string & text, c_options * options, string * status)
  {
    vector<string> parts;
    vector<pair<string, string> > opts;

    split(text, ",", &parts);

    for ( unsigned i = 0; i < parts.size(); i++ ) {
      const string::size_type equals_pos = parts[i].find_first_of('=');
      if ( equals_pos == string::npos ) {
        opts.push_back(make_pair(parts[i], ""));
      }
      else {
        opts.push_back(make_pair(parts[i].substr(0, equals_pos), parts[i].substr(equals_pos + 1)));
      }
    }

    for (size_t i = 0; i < opts.size(); i++) {
      if ( opts[i].first == "no-pkgprefix") {
        options->do_pkgprefix = false;
      }
      else {
        *status = "Unknown option: " + opts[i].first;
        return false;
      }
    }

    return true;
  }


  void generate_c_header(const FileDescriptor * file, Printer * printer)
  {
    // static const int min_header_version = 1000000;

    const string fileid = file_id(file->name());

    // Generate top of header.
    printer->Print(""
        "/*\n"
        " * Generated by the protocol buffer compiler from $filename$\n"
        " * DO NOT EDIT!\n"
        " */\n"
        "#ifndef __$fileid$_h__\n"
        "#define __$fileid$_h__\n"
        "\n",
        "filename", file->name(),
        "fileid", fileid
        );

    printer->Print("#include <stddef.h>\n");
    printer->Print("#include <stdint.h>\n");
    printer->Print("#include <stdbool.h>\n");
    printer->Print("#include \"cf_pb.h\"\n");

    for ( int i = 0; i < file->dependency_count(); i++ ) {
      printer->Print("#include \"$dependency$.pb-c.h\"\n", "dependency",
          strip_proto_suffix(file->dependency(i)->name()));
    }

    printer->Print("\n\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n");


    if ( generate_enum_declarations(file, printer) ) {
      printer->Print("\n");
    }

    if ( generate_case_enums_for_unions(file, printer) ) {
      printer->Print("\n");
    }

    if ( file->message_type_count() > 0 ) {
      // Generate structs.
      generate_structs(file, printer);
      printer->Print("\n");
    }

    printer->Print("\n"
        "#ifdef __cplusplus\n"
        "} /* extern \"C\" */\n"
        "#endif\n"
        "\n\n#endif  /* __$fileid$_h__ */\n",
        "fileid", fileid
        );

  }


  void generate_c_source(const FileDescriptor * file, Printer * printer)
  {
    // Generate top of header.
    printer->Print(""
        "/*\n"
        " * Generated by the protocol buffer compiler from $filename$\n"
        " * DO NOT EDIT!\n"
        " */\n",
        "filename",
        file->name());


    printer->Print("#include \"$filename$.pb-c.h\"\n", "filename", strip_proto_suffix(file->name()));
    printer->Print("\n\n");


    if ( file->message_type_count() > 0 ) {
      generate_pb_fields(file, printer);
      printer->Print("\n");
    }

//    // Generate function definitions
//    if ( file->message_type_count() > 0 ) {
//      Generate_ZRealloc(printer);
//      printer->Print("\n\n");
//      GenerateMethodDefinitions(file, printer);
//      printer->Print("\n");
//    }
//
//    // Generate services
//    if ( file->service_count() > 0 ) {
//      GenerateServiceDefinitions(file, printer);
//    }

  }


  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Generate enum declarations

  string enum_type_name(const EnumDescriptor * enum_type)
  {
    return full_name(enum_type);
  }

  string enum_member_name(const EnumValueDescriptor * enum_member)
  {
    return full_name(enum_member);
  }

  int generate_enum_declarations(const FileDescriptor * file, Printer * printer)
  {
    int enums_generated = 0;

    for ( int i = 0, n = file->enum_type_count(); i < n; ++i, ++enums_generated ) {

      const EnumDescriptor * enum_type = file->enum_type(i);
      const string type_name = enum_type_name(enum_type);

      printer->Print("typedef\n"
          "enum $type_name$ { \n",
          "type_name", type_name);

      printer->Indent();

      for ( int j = 0, m = enum_type->value_count(); j < m; ++j ) {
        const EnumValueDescriptor * enum_member = enum_type->value(j);
        printer->Print("$name$ = $value$,\n", "name", enum_member_name(enum_member), "value",
            t2s(enum_member->number()));
      }
      printer->Outdent();
      printer->Print("} $type_name$;\n\n", "type_name", type_name);
    }

    return enums_generated;
  }



  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Generate the case enums for unions

  string oneof_enum_type_name(const OneofDescriptor * oneof)
  {
    return full_name(oneof) + "_tag";
  }

  string oneof_enum_member_name(const OneofDescriptor * oneof, const FieldDescriptor * field)
  {
    return full_name(oneof) + "_" + (field ? field_name(field) : string("none"));
  }

  int generate_case_enums_for_unions(const FileDescriptor * file, Printer * printer)
  {
    int enums_generated = 0;
    const Descriptor * type;
    const OneofDescriptor * oneof;
    const FieldDescriptor * field;

    for ( int j = 0, m = file->message_type_count(); j < m; ++j ) {

      type = file->message_type(j);

      for ( int i = 0, n = type->oneof_decl_count(); i < n; ++i, ++enums_generated ) {

        oneof = type->oneof_decl(i);
        const string enum_name = oneof_enum_type_name(oneof);

        printer->Print("typedef\n"
            "enum $enum_name$ {\n",
            "enum_name", enum_name);
        printer->Indent();

        printer->Print("$member_name$ = 0,\n", "member_name", oneof_enum_member_name(oneof, NULL));
        for ( int k = 0, l = oneof->field_count(); k < l; ++k ) {
          field = oneof->field(k);
          printer->Print("$name$ = $value$,\n", "name", oneof_enum_member_name(oneof, field), "value",
              t2s(field->number()));
        }

        printer->Outdent();
        printer->Print("} $enum_name$;\n\n", "enum_name", enum_name);
      }
    }
    return enums_generated;
  }


  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Generate structs

  void generate_structs(const FileDescriptor * file, Printer * printer)
  {
    for ( int i = 0, n = file->message_type_count(); i < n; ++i ) {
      generate_struct(file->message_type(i), printer);
      printer->Print("\n");
    }
  }

  void generate_struct(const Descriptor * type, Printer * printer)
  {
    map<string, string> vars;
    const FieldDescriptor * field;
    const OneofDescriptor * oneof;

    for ( int i = 0, n = type->nested_type_count(); i < n; ++i ) {
      generate_struct(type->nested_type(i), printer);
      printer->Print("\n");
    }

    vars["class_name"] = full_name(type);

    // Generate struct fields
    printer->Print(vars,
        "typedef\n"
        "struct $class_name$ {\n");
    printer->Indent();

    for ( int i = 0, n = type->field_count(); i < n; ++i ) {
      if ( !(field = type->field(i))->containing_oneof() ) {
        generate_struct_member(field, printer);
      }
    }

    // Generate unions from oneofs.
    for ( int j = 0, m = type->oneof_decl_count(); j < m; ++j ) {

      oneof = type->oneof_decl(j);
      const string enum_name = oneof_enum_type_name(oneof);

      printer->Print("\nstruct {\n");
      printer->Indent();
      printer->Print("uint32_t tag; /* enum $enum_name$ */\n", "enum_name", enum_name);
      printer->Print("union {\n");
      printer->Indent();
      for ( int k = 0, l = oneof->field_count(); k < l; ++k ) {
        generate_struct_member(oneof->field(k), printer);
      }
      printer->Outdent();
      printer->Print("}\n");
      printer->Outdent();
      printer->Print("} $name$;\n\n", "name", name(oneof));
    }


    for ( int i = 0, n = type->field_count(); i < n; ++i ) {
      if ( !(field = type->field(i))->containing_oneof() ) {
        generate_struct_has_member(field, printer);
      }
    }


    printer->Outdent();
    printer->Print(vars, "} $class_name$;\n\n");
    printer->Print(vars, "extern const cf_pb_field_t $class_name$_fields[];\n\n");

  }


  void generate_struct_member(const FieldDescriptor * field, Printer * printer)
  {
    map<string, string> vars;

    vars["field_name"] = field_name(field);
    vars["field_type"] = cfctype(field);

    if ( field->label() == FieldDescriptor::LABEL_REPEATED ) {
      printer->Print(vars, "ccarray_t $field_name$; /* <$field_type$> */\n");
    }
    else if ( field->type() == FieldDescriptor::TYPE_BYTES ) {
      printer->Print(vars, "cf_membuf $field_name$; /* <$field_type$> */\n");
    }
    else {
      printer->Print(vars, "$field_type$ $field_name$;\n");
    }
  }

  void generate_struct_has_member(const FieldDescriptor * field, Printer * printer)
  {
    if ( field->label() == FieldDescriptor::LABEL_OPTIONAL && !field->containing_oneof() ) {
      printer->Print("bool has_$name$;\n", "name", field_name(field));
    }
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  void generate_pb_fields(const FileDescriptor * file, Printer * printer)
  {
    for ( int i = 0, n = file->message_type_count(); i < n; ++i ) {
      generate_pb_fields_for_struct(file->message_type(i), printer);
      printer->Print("\n");
    }
  }

  void generate_pb_fields_for_struct(const Descriptor * type, Printer * printer)
  {
    map<string, string> vars;
    const FieldDescriptor * field;
    int i, n;

    for ( i = 0, n = type->nested_type_count(); i < n; ++i ) {
      generate_pb_fields_for_struct(type->nested_type(i), printer);
      printer->Print("\n");
    }

    vars["class_name"] = full_name(type);

    printer->Print(vars, "const cf_pb_field_t $class_name$_fields[] = {\n");
    printer->Indent();

    for ( i = 0, n = type->field_count(); i < n; ++i ) {
      field = type->field(i);

      vars["tag"] = t2s(field->number());
      vars["name"] = field_name(field);
      vars["pbtype"] = cfpbtype(field);
      vars["ctype"] = cfctype(field);
      vars["ptr"] = cfdescptr(field);

      if ( field->containing_oneof() ) {
        // CF_PB_ONEOF_FIELD
        vars["oneof"] = name(field->containing_oneof());
        printer->Print(vars,
            "CF_PB_ONEOF_FIELD   ($class_name$,\t$tag$,\tCF_PB_$pbtype$,\t$oneof$,\t$name$,\t$ctype$,\t$ptr$),\n");
      }
      else if ( field->label() == FieldDescriptor::LABEL_REPEATED ) {
        printer->Print(vars,
            "CF_PB_REQUIRED_FIELD($class_name$,\t$tag$,\tCF_PB_$pbtype$,\tCF_PB_ARRAY ,\t$name$,\t$ctype$,\t$ptr$),\n");
      }
      else if ( field->label() == FieldDescriptor::LABEL_REQUIRED ) {
        printer->Print(vars,
            "CF_PB_REQUIRED_FIELD($class_name$,\t$tag$,\tCF_PB_$pbtype$,\tCF_PB_SCALAR, $name$, $ctype$, $ptr$),\n");
      }
      else {
        printer->Print(vars,
            "CF_PB_OPTIONAL_FIELD($class_name$,\t$tag$,\tCF_PB_$pbtype$,\tCF_PB_SCALAR, $name$, $ctype$, $ptr$),\n");
      }
    }

    printer->Print("CF_PB_LAST_FIELD\n");
    printer->Outdent();
    printer->Print(vars, "};\n\n");
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  template<class T>
  string name(const T * t)
  {
    const string name = t->name();
    vector<string> pieces;
    string rv = "";

    split(name, ".", &pieces);
    for ( size_t i = 0; i < pieces.size(); i++ ) {
      if ( !pieces[i].empty() ) {
        if ( !rv.empty() ) {
          rv += "_";
        }
        rv += pieces[i];
      }
    }
    return rv;
  }

  template<class T>
  string full_name(const T * t)
  {
    const string fullname = t->full_name();
    vector<string> pieces;
    string rv = "";

    split(fullname, ".", &pieces);
    for ( size_t i = options.do_pkgprefix ? 0 : 1, n = pieces.size(); i < n; ++i ) {
      if ( !pieces[i].empty() ) {
        if ( !rv.empty() ) {
          rv += "_";
        }
        rv += pieces[i];
      }
    }
    return rv;
  }

  string field_name(const FieldDescriptor * field)
  {
    return name(field);
  }

  string cfctype(const FieldDescriptor * field)
  {
    switch ( field->type() ) {
      case FieldDescriptor::TYPE_DOUBLE :    // double, exactly eight bytes on the wire.
        return "double";
      case FieldDescriptor::TYPE_FLOAT :    // float, exactly four bytes on the wire.
        return "float";
      case FieldDescriptor::TYPE_INT32 :    // int32, varint on the wire
      case FieldDescriptor::TYPE_SFIXED32 :    // int32, exactly four bytes on the wire
      case FieldDescriptor::TYPE_SINT32 :    // int32, ZigZag-encoded varint on the wire
        return "int32_t";
      case FieldDescriptor::TYPE_UINT32 :    // uint32, varint on the wire
      case FieldDescriptor::TYPE_FIXED32 :    // uint32, exactly four bytes on the wire.
        return "uint32_t";
      case FieldDescriptor::TYPE_INT64 :    // int64, varint on the wire.
      case FieldDescriptor::TYPE_SFIXED64 :    // int64, exactly eight bytes on the wire
      case FieldDescriptor::TYPE_SINT64 :    // int64, ZigZag-encoded varint on the wire
        return "int64_t";
      case FieldDescriptor::TYPE_UINT64 :    // uint64, varint on the wire.
      case FieldDescriptor::TYPE_FIXED64 :    // uint64, exactly eight bytes on the wire.
        return "uint64_t";
      case FieldDescriptor::TYPE_BOOL :    // bool, varint on the wire.
        return "bool";
      case FieldDescriptor::TYPE_ENUM :    // Enum, varint on the wire
        return "enum " + full_name(field->enum_type());
      case FieldDescriptor::TYPE_STRING :    // String
        return "char *";
      case FieldDescriptor::TYPE_GROUP :      // Tag-delimited message.  Deprecated.
      case FieldDescriptor::TYPE_MESSAGE :    // Length-delimited message.
        return "struct " + full_name(field->message_type());
      case FieldDescriptor::TYPE_BYTES :    // Arbitrary byte array.
        return "uint8_t";

      default:
        break;
    }
    return "";
  }

  string cfdescptr(const FieldDescriptor * field)
  {
    switch ( field->type() ) {
    case FieldDescriptor::TYPE_GROUP :
    case FieldDescriptor::TYPE_MESSAGE :
      return full_name(field->message_type()) + "_fields";
    default:
      break;
    }
    return "NULL";
  }

  string cfpbtype(const FieldDescriptor * field)
  {
    switch ( field->type() ) {
    case FieldDescriptor::TYPE_INT32    :    // int32, varint on the wire
      return "INT32   ";
    case FieldDescriptor::TYPE_SINT32   :    // int32, ZigZag-encoded varint on the wire
      return "INT32   ";
    case FieldDescriptor::TYPE_SFIXED32 :    // int32, exactly four bytes on the wire
      return "SFIXED32";
    case FieldDescriptor::TYPE_UINT32   :    // uint32, varint on the wire
      return "UINT32  ";
    case FieldDescriptor::TYPE_FIXED32  :    // uint32, exactly four bytes on the wire.
      return "FIXED32 ";

    case FieldDescriptor::TYPE_INT64    :    // int64, varint on the wire.
      return "INT64   ";
    case FieldDescriptor::TYPE_SINT64   :    // int64, ZigZag-encoded varint on the wire
      return "INT64   ";
    case FieldDescriptor::TYPE_SFIXED64 :    // int64, exactly eight bytes on the wire
      return "SFIXED64";
    case FieldDescriptor::TYPE_UINT64   :    // uint64, varint on the wire.
      return "UINT64  ";
    case FieldDescriptor::TYPE_FIXED64  :    // uint64, exactly eight bytes on the wire.
      return "FIXED64 ";

    case FieldDescriptor::TYPE_BOOL     :    // bool, varint on the wire.
      return "BOOL    ";

    case FieldDescriptor::TYPE_DOUBLE   :    // double, exactly eight bytes on the wire.
      return "DOUBLE  ";

    case FieldDescriptor::TYPE_FLOAT    :    // float, exactly four bytes on the wire.
      return "FLOAT   ";

    case FieldDescriptor::TYPE_ENUM     :    // Enum, varint on the wire
      return "ENUM    ";

    case FieldDescriptor::TYPE_STRING   :
      return "STRING  ";

    case FieldDescriptor::TYPE_BYTES    :
      return "BYTES   ";

    case FieldDescriptor::TYPE_GROUP :
    case FieldDescriptor::TYPE_MESSAGE :
      return "MESSAGE ";

    default:
        break;
    }

    return "BUG-HERE";
  }

  string nanopb_allocation(const FieldDescriptor * field)
  {
    if ( field->label() == FieldDescriptor::LABEL_REPEATED ) {
      return "CALLBACK";
    }

    switch ( field->type() ) {
      case FieldDescriptor::TYPE_DOUBLE :
      case FieldDescriptor::TYPE_FLOAT :
      case FieldDescriptor::TYPE_INT32 :
      case FieldDescriptor::TYPE_SFIXED32 :
      case FieldDescriptor::TYPE_SINT32 :
      case FieldDescriptor::TYPE_UINT32 :
      case FieldDescriptor::TYPE_FIXED32 :
      case FieldDescriptor::TYPE_INT64 :
      case FieldDescriptor::TYPE_SFIXED64 :
      case FieldDescriptor::TYPE_SINT64 :
      case FieldDescriptor::TYPE_UINT64 :
      case FieldDescriptor::TYPE_FIXED64 :
      case FieldDescriptor::TYPE_BOOL :
      case FieldDescriptor::TYPE_ENUM :
        return "STATIC";

      case FieldDescriptor::TYPE_STRING :
      case FieldDescriptor::TYPE_MESSAGE :
        return "POINTER";

      case FieldDescriptor::TYPE_BYTES :
        return "CALLBACK";

      default:
        break;
    }
    return "";
  }

  // Convert a file name into a valid identifier.
  static string file_id(const string & filename) {
    string s;
    for ( size_t i = 0; i < filename.size(); i++ ) {
      s.push_back(isalnum(filename[i]) ? filename[i] : '_');
    }
    return s;
  }

  //////////////////////////

  static bool has_suffix(const string & str, const string & suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  static string strip_suffix(const string& str, const string& suffix) {
    return has_suffix(str, suffix) ? str.substr(0, str.size() - suffix.size()) : str;
  }

  static string strip_proto_suffix(const string & filename) {
    return has_suffix(filename, ".protodevel") ? strip_suffix(filename, ".protodevel") : strip_suffix(filename, ".proto");
  }

  static string t2s(int x) {
    char s[16] = "";
    sprintf(s, "%2d", x);
    return s;
  }

  //////////////////////////
  static void split(const string & full, const char* delim, vector<string>* result)
  {
    back_insert_iterator<vector<string> > output(*result);
    split_to_iterator(full, delim, output);
  }

  template<typename _OI>
  static inline void split_to_iterator(const string & full, const char delim[], _OI & output)
  {
    // Optimize the common case where delim is a single character.
    if ( delim[0] != '\0' && delim[1] == '\0' ) {
      char c = delim[0];
      const char * p = full.data();
      const char * end = p + full.size();
      while ( p != end ) {
        if ( *p == c ) {
          ++p;
        }
        else {
          const char * start = p;
          while ( ++p != end && *p != c )
            ;
          *output++ = string(start, p - start);
        }
      }
    }
    else {
      string::size_type begin_index, end_index;
      begin_index = full.find_first_not_of(delim);
      while ( begin_index != string::npos ) {
        end_index = full.find_first_of(delim, begin_index);
        if ( end_index == string::npos ) {
          *output++ = full.substr(begin_index);
          return;
        }
        *output++ = full.substr(begin_index, (end_index - begin_index));
        begin_index = full.find_first_not_of(delim, end_index);
      }
    }
  }
  //////////////////////////





};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CORPC_C_Generator
      : public CodeGenerator
{
  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(CORPC_C_Generator);
  C_Generator * c_generator;
public:
  CORPC_C_Generator() {
    c_generator = new C_Generator();
  }

  ~CORPC_C_Generator() {
    delete c_generator;
  }

  bool Generate(const FileDescriptor * file, const string & options, GeneratorContext * gctx, string * status) const {
    return c_generator->Generate(file, options, gctx, status);
  }
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
  CommandLineInterface cli;
  CORPC_C_Generator generator;

  cli.RegisterGenerator("--c_out", "--c_opt", &generator, "Generate C/H files.");

  // Add version info generated by automake
  // cli.SetVersionInfo(PACKAGE_STRING);

  fprintf(stderr, "cli.Run()\n");

  for ( int i = 0; i < argc; ++i ) {
    fprintf(stderr, "arg[%d]=%s\n", i, argv[i]);
  }

  return cli.Run(argc, argv);
}




