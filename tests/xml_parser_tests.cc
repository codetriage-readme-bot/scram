/*
 * Copyright (C) 2014-2016 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xml_parser_tests.h"

#include "error.h"
#include "relax_ng_validator.h"

namespace scram {
namespace test {

void XmlParserTests::FillSnippet(std::stringstream& ss, bool malformed) {
  ss << "<" << outer_node_ << ">";
  if (!malformed)
    ss << "<" << inner_node_ << ">" << inner_content_ << "</" << inner_node_
       << ">";
  ss << "</" << outer_node_ << ">";
}

void XmlParserTests::FillSchema(std::stringstream& ss, bool malformed) {
  ss << "<grammar xmlns=\"http://relaxng.org/ns/structure/1.0\"\n"
     << "datatypeLibrary=\"http://www.w3.org/2001/XMLSchema-datatypes\">\n"
     << "  " << "<start>" << "\n"
     << "  " << "<element " << (malformed ? "naem" : "name") << " =\""
     << outer_node_ << "\">\n"
     << "  " << "  " << "<element name =\"" << inner_node_ << "\">\n"
     << "  " << "    " << "<text/>\n"
     << "  " << "  " << "</element>\n"
     << "  " << "</element>\n"
     << "  " << "</start>\n"
     << "</grammar>";
}

using XmlParserPtr = std::unique_ptr<XmlParser>;

// This is an indirect test of the validator.
TEST_F(XmlParserTests, RelaxNGValidator) {
  std::stringstream snippet;
  FillSnippet(snippet);
  std::stringstream schema;
  FillSchema(schema);

  XmlParserPtr parser;
  ASSERT_NO_THROW(parser = XmlParserPtr(new XmlParser(snippet)));

  RelaxNGValidator validator;
  EXPECT_THROW(validator.Validate(nullptr), LogicError);
  const xmlpp::Document* doc = parser->Document();
  EXPECT_THROW(validator.Validate(doc), LogicError);  // No schema initialized.

  ASSERT_NO_THROW(validator.ParseMemory(schema.str()));
  EXPECT_NO_THROW(validator.Validate(doc));  // Initialized.
}

TEST_F(XmlParserTests, WithoutSchema) {
  std::stringstream snippet;
  FillSnippet(snippet);
  EXPECT_NO_THROW(XmlParserPtr(new XmlParser(snippet)));
}

TEST_F(XmlParserTests, WithSchema) {
  std::stringstream snippet;
  FillSnippet(snippet);
  std::stringstream schema;
  FillSchema(schema);
  XmlParserPtr parser;
  ASSERT_NO_THROW(parser = XmlParserPtr(new XmlParser(snippet)));
  EXPECT_NO_THROW(parser->Validate(schema));
}

TEST_F(XmlParserTests, WithBadSchema) {
  std::stringstream snippet;
  FillSnippet(snippet);
  std::stringstream schema;
  FillSchema(schema, /*malformed=*/true);
  XmlParserPtr parser;
  ASSERT_NO_THROW(parser = XmlParserPtr(new XmlParser(snippet)));
  EXPECT_THROW(parser->Validate(schema), LogicError);
}

TEST_F(XmlParserTests, WithError) {
  std::stringstream snippet;
  FillSnippet(snippet, /*malformed=*/true);
  std::stringstream schema;
  FillSchema(schema);
  XmlParserPtr parser;
  ASSERT_NO_THROW(parser = XmlParserPtr(new XmlParser(snippet)));
  EXPECT_THROW(parser->Validate(schema), ValidationError);
}

}  // namespace test
}  // namespace scram
