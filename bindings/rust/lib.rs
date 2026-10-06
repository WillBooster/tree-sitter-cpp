//! This crate provides C++ language support for the [tree-sitter] parsing library.
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
//! [`Parser`]: https://docs.rs/willbooster-tree-sitter/1/tree_sitter/struct.Parser.html
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
        let language = super::LANGUAGE.into();
        let mut parser = tree_sitter::Parser::new();
        parser.set_language(&language).unwrap();
        let tree = parser.parse(source, None).unwrap();
        assert!(!tree.root_node().has_error());

        let query = tree_sitter::Query::new(&language, super::TAGS_QUERY).unwrap();
        let name_index = query.capture_index_for_name("name").unwrap();
        let mut cursor = tree_sitter::QueryCursor::new();
        let mut captures = cursor.captures(&query, tree.root_node(), source.as_bytes());
        let mut names = Vec::new();
        while let Some((query_match, capture_index)) = captures.next() {
            let capture = query_match.captures()[*capture_index];
            if capture.index == name_index {
                names.push(capture.node.utf8_text(source.as_bytes()).unwrap());
            }
        }
        assert_eq!(names, ["Example", "method", "function"]);
    }
}
