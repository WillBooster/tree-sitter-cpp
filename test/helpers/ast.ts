import type { Node } from '@willbooster/web-tree-sitter';

export interface AstCapture {
  name: string;
  node: Node;
}

export type CaptureVisitor = (node: Node, emit: (name: string, target: Node | null | undefined) => void) => void;

export function collectCaptures(root: Node, visit: CaptureVisitor): AstCapture[] {
  const captures: AstCapture[] = [];
  const emit = (name: string, target: Node | null | undefined): void => {
    if (target) captures.push({ name, node: target });
  };
  walkNamedPreOrder(root, (node) => visit(node, emit));
  return captures.toSorted(
    (left, right) => left.node.startIndex - right.node.startIndex || right.node.endIndex - left.node.endIndex
  );
}

function walkNamedPreOrder(node: Node, visit: (current: Node) => void): void {
  visit(node);
  for (const child of node.namedChildren) {
    walkNamedPreOrder(child, visit);
  }
}

export function captureNodes(captures: readonly AstCapture[], name: string): Node[] {
  return captures.filter((capture) => capture.name === name).map((capture) => capture.node);
}

export function trailingReturnDeclarators(root: Node): Node[] {
  return root
    .descendantsOfType('trailing_return_type')
    .flatMap((node) => childrenOfType(node, 'type_descriptor'))
    .flatMap((descriptor) => fieldChild(descriptor, 'declarator') ?? []);
}

export function childrenOfType(node: Node | null | undefined, types: string | readonly string[]): Node[] {
  if (!node) return [];
  return node.namedChildren.filter((child) => matchesType(child, types));
}

export function fieldChild(
  node: Node | null | undefined,
  field: string,
  types?: string | readonly string[]
): Node | undefined {
  const child = node?.childForFieldName(field) ?? undefined;
  if (!child || (types !== undefined && !matchesType(child, types))) return undefined;
  return child;
}

export function fieldChildren(
  node: Node | null | undefined,
  field: string,
  types?: string | readonly string[]
): Node[] {
  if (!node) return [];
  const children = node.childrenForFieldName(field);
  return types === undefined ? children : children.filter((child) => matchesType(child, types));
}

function matchesType(node: Node, types: string | readonly string[]): boolean {
  return typeof types === 'string' ? node.type === types : types.includes(node.type);
}

export function pointAt(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}
