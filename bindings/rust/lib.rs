//! This crate provides C++ language support for the [tree-sitter] parsing library.
//!
//! The parser carries the ABI number of the WillBooster/tree-sitter fork, so it loads only with
//! [willbooster-tree-sitter] 7.0.5 or later within major version 7; upstream's runtime rejects it.
//!
//! Typically, you will use the [`LANGUAGE`] constant to add this language to a
//! tree-sitter [`Parser`], and then use the parser to parse some code:
//!
//! ```
//! let code = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello\\n\";\n}\n";
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_cpp::LANGUAGE;
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading C++ parser");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [`Parser`]: https://docs.rs/willbooster-tree-sitter/7/tree_sitter/struct.Parser.html
//! [willbooster-tree-sitter]: https://crates.io/crates/willbooster-tree-sitter
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

unsafe extern "C" {
    fn tree_sitter_cpp() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for this grammar.
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_cpp) };

/// The content of the [`node-types.json`] file for this grammar.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The query for extracting symbol definitions from C++ source.
pub const TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

/// The query for identifying languages and contents embedded in C++ raw strings.
pub const INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

#[cfg(test)]
mod tests {
    use tree_sitter::StreamingIterator;

    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::LANGUAGE.into())
            .expect("Error loading C++ parser");
    }

    #[test]
    fn extracts_symbol_definitions() {
        let source = "struct Example { void method() {} };\nvoid function() {}\n";
        assert_eq!(
            captured_texts(source, super::TAGS_QUERY, "name"),
            ["Example", "method", "function"]
        );
    }

    #[test]
    fn extracts_raw_string_injections() {
        let source =
            "const char* embedded = R\"sql(SELECT 1)sql\";\nconst char* plain = \"SELECT 2\";\n";
        assert_eq!(
            captured_texts(source, super::INJECTIONS_QUERY, "injection.language"),
            ["sql"]
        );
        assert_eq!(
            captured_texts(source, super::INJECTIONS_QUERY, "injection.content"),
            ["SELECT 1"]
        );
    }

    fn captured_texts(source: &str, query_source: &str, capture_name: &str) -> Vec<String> {
        let language = super::LANGUAGE.into();
        let mut parser = tree_sitter::Parser::new();
        parser.set_language(&language).unwrap();
        let tree = parser.parse(source, None).unwrap();
        assert!(!tree.root_node().has_error());

        let query = tree_sitter::Query::new(&language, query_source).unwrap();
        let name_index = query.capture_index_for_name(capture_name).unwrap();
        let mut cursor = tree_sitter::QueryCursor::new();
        let mut captures = cursor.captures(&query, tree.root_node(), source.as_bytes());
        let mut texts = Vec::new();
        while let Some((query_match, capture_index)) = captures.next() {
            let capture = query_match.captures()[*capture_index];
            if capture.index == name_index {
                texts.push(
                    capture
                        .node
                        .utf8_text(source.as_bytes())
                        .unwrap()
                        .to_owned(),
                );
            }
        }
        texts
    }
}
