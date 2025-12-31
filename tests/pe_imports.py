#!/usr/bin/env python3
"""
PE Import Table Dumper with Recursive DLL Dependencies
Prints all DLL dependencies and imported functions from a Windows executable.
Can recursively resolve the full dependency tree.
"""

import pefile
import sys
import os
from pathlib import Path

# Common Windows system DLL search paths
WINDOWS_DLL_PATHS = [
    r"C:\Windows\System32",
    r"C:\Windows\SysWOW64",
    r"C:\Windows",
]


def find_dll(dll_name, search_paths):
    """Try to find a DLL in the given search paths."""
    for path in search_paths:
        full_path = os.path.join(path, dll_name)
        if os.path.exists(full_path):
            return full_path
    return None


def get_imports(pe_path):
    """Get list of imported DLLs and functions from a PE file."""
    try:
        pe = pefile.PE(pe_path, fast_load=True)
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
            pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']
        ])
    except Exception as e:
        return None, None, str(e)
    
    imports = {}
    delay_imports = {}
    
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll_name = entry.dll.decode('utf-8', errors='replace')
            functions = []
            for imp in entry.imports:
                if imp.name:
                    functions.append({
                        'name': imp.name.decode('utf-8', errors='replace'),
                        'ordinal': imp.ordinal,
                        'address': imp.address
                    })
                else:
                    functions.append({
                        'name': None,
                        'ordinal': imp.ordinal,
                        'address': imp.address
                    })
            imports[dll_name] = functions
    
    if hasattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT'):
        for entry in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
            dll_name = entry.dll.decode('utf-8', errors='replace')
            functions = []
            for imp in entry.imports:
                if imp.name:
                    functions.append({
                        'name': imp.name.decode('utf-8', errors='replace'),
                        'ordinal': imp.ordinal,
                        'address': imp.address
                    })
                else:
                    functions.append({
                        'name': None,
                        'ordinal': imp.ordinal,
                        'address': imp.address
                    })
            delay_imports[dll_name] = functions
    
    pe.close()
    return imports, delay_imports, None


def print_imports(pe_path, show_functions=True):
    """Parse and print the import table of a PE file."""
    
    if not os.path.exists(pe_path):
        print(f"Error: File '{pe_path}' not found.")
        return False
    
    imports, delay_imports, error = get_imports(pe_path)
    
    if error:
        print(f"Error: {error}")
        return False
    
    print(f"\n{'='*60}")
    print(f"Import Table for: {os.path.basename(pe_path)}")
    print(f"{'='*60}\n")
    
    if not imports and not delay_imports:
        print("No import table found (statically linked or packed?)")
        return True
    
    total_dlls = 0
    total_functions = 0
    
    for dll_name, functions in imports.items():
        total_dlls += 1
        print(f"[{dll_name}]")
        
        if show_functions:
            for func in functions:
                total_functions += 1
                if func['name']:
                    print(f"    {func['ordinal'] or '-':>5}  0x{func['address']:08X}  {func['name']}")
                else:
                    print(f"    {func['ordinal']:>5}  0x{func['address']:08X}  (ordinal only)")
        else:
            total_functions += len(functions)
        
        print()
    
    if delay_imports:
        print(f"{'='*60}")
        print("Delay-Load Imports:")
        print(f"{'='*60}\n")
        
        for dll_name, functions in delay_imports.items():
            total_dlls += 1
            print(f"[{dll_name}] (delay-loaded)")
            
            if show_functions:
                for func in functions:
                    total_functions += 1
                    if func['name']:
                        print(f"    {func['ordinal'] or '-':>5}  0x{func['address']:08X}  {func['name']}")
                    else:
                        print(f"    {func['ordinal']:>5}  0x{func['address']:08X}  (ordinal only)")
            else:
                total_functions += len(functions)
            print()
    
    print(f"{'='*60}")
    print(f"Summary: {total_dlls} DLLs, {total_functions} imported functions")
    print(f"{'='*60}")
    
    return True


def build_dependency_tree(pe_path, search_paths=None, max_depth=10):
    """
    Recursively build a full dependency tree.
    Returns a dict: {dll_name: {children: [...], path: ..., error: ...}}
    """
    if search_paths is None:
        search_paths = []
    
    # Add the directory of the main executable to search paths
    exe_dir = os.path.dirname(os.path.abspath(pe_path))
    all_search_paths = [exe_dir] + search_paths + WINDOWS_DLL_PATHS
    
    visited = set()
    
    def resolve(file_path, name, depth):
        if depth > max_depth:
            return {'name': name, 'children': [], 'error': 'max depth reached'}
        
        name_lower = name.lower()
        if name_lower in visited:
            return {'name': name, 'children': [], 'circular': True}
        
        visited.add(name_lower)
        
        imports, delay_imports, error = get_imports(file_path)
        
        if error:
            return {'name': name, 'path': file_path, 'children': [], 'error': error}
        
        all_imports = {}
        if imports:
            all_imports.update(imports)
        if delay_imports:
            for k, v in delay_imports.items():
                all_imports[k + " (delay)"] = v
        
        children = []
        for dll_name in all_imports.keys():
            clean_name = dll_name.replace(" (delay)", "")
            dll_path = find_dll(clean_name, all_search_paths)
            
            if dll_path:
                child = resolve(dll_path, dll_name, depth + 1)
                child['path'] = dll_path
            else:
                child = {'name': dll_name, 'children': [], 'not_found': True}
            
            children.append(child)
        
        return {'name': name, 'path': file_path, 'children': children}
    
    return resolve(pe_path, os.path.basename(pe_path), 0)


def print_tree(node, indent=0, printed=None):
    """Print the dependency tree."""
    if printed is None:
        printed = set()
    
    prefix = "  " * indent
    name = node['name']
    
    # Status indicators
    status = ""
    if node.get('circular'):
        status = " (circular ref)"
    elif node.get('not_found'):
        status = " [NOT FOUND]"
    elif node.get('error'):
        status = f" [ERROR: {node['error']}]"
    
    print(f"{prefix}├── {name}{status}")
    
    # Track printed to avoid excessive output
    name_lower = name.lower()
    if name_lower in printed:
        if node['children']:
            print(f"{prefix}    └── ...")
        return
    printed.add(name_lower)
    
    for child in node.get('children', []):
        print_tree(child, indent + 1, printed)


def print_flat_dependencies(pe_path, search_paths=None):
    """Print all unique DLL dependencies (flat list, recursive)."""
    
    if search_paths is None:
        search_paths = []
    
    exe_dir = os.path.dirname(os.path.abspath(pe_path))
    all_search_paths = [exe_dir] + search_paths + WINDOWS_DLL_PATHS
    
    all_dlls = {}  # name -> {'path': ..., 'found': bool}
    to_process = [(pe_path, os.path.basename(pe_path))]
    processed = set()
    
    while to_process:
        file_path, name = to_process.pop(0)
        name_lower = name.lower()
        
        if name_lower in processed:
            continue
        processed.add(name_lower)
        
        imports, delay_imports, error = get_imports(file_path)
        
        if error:
            continue
        
        all_imports = set()
        if imports:
            all_imports.update(imports.keys())
        if delay_imports:
            all_imports.update(delay_imports.keys())
        
        for dll_name in all_imports:
            dll_lower = dll_name.lower()
            if dll_lower not in all_dlls:
                dll_path = find_dll(dll_name, all_search_paths)
                all_dlls[dll_lower] = {
                    'name': dll_name,
                    'path': dll_path,
                    'found': dll_path is not None
                }
                if dll_path:
                    to_process.append((dll_path, dll_name))
    
    # Print results
    print(f"\n{'='*60}")
    print(f"All Dependencies for: {os.path.basename(pe_path)}")
    print(f"{'='*60}\n")
    
    found = [(k, v) for k, v in all_dlls.items() if v['found']]
    not_found = [(k, v) for k, v in all_dlls.items() if not v['found']]
    
    print(f"Found ({len(found)}):")
    for _, info in sorted(found):
        print(f"  {info['name']}")
        print(f"      {info['path']}")
    
    if not_found:
        print(f"\nNot Found ({len(not_found)}):")
        for _, info in sorted(not_found):
            print(f"  {info['name']}")
    
    print(f"\n{'='*60}")
    print(f"Total: {len(all_dlls)} unique DLLs")
    print(f"{'='*60}")


def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Dump PE import table and DLL dependencies',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s program.exe                    # Show imports with functions
  %(prog)s program.exe --simple           # Show only DLL names  
  %(prog)s program.exe --recursive        # Resolve all DLL dependencies
  %(prog)s program.exe --tree             # Show dependency tree
  %(prog)s program.exe -r -p C:\\MyDLLs   # Add custom search path
        '''
    )
    
    parser.add_argument('file', help='PE file to analyze (.exe, .dll, .sys)')
    parser.add_argument('--simple', '-s', action='store_true',
                        help='Show only DLL names (no functions)')
    parser.add_argument('--recursive', '-r', action='store_true',
                        help='Recursively resolve all DLL dependencies')
    parser.add_argument('--tree', '-t', action='store_true',
                        help='Show dependency tree (implies --recursive)')
    parser.add_argument('--path', '-p', action='append', default=[],
                        help='Additional DLL search path (can be used multiple times)')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.file):
        print(f"Error: File '{args.file}' not found.")
        sys.exit(1)
    
    if args.tree:
        print(f"\nDependency Tree for: {os.path.basename(args.file)}")
        print("=" * 60)
        tree = build_dependency_tree(args.file, args.path)
        print_tree(tree)
    elif args.recursive:
        print_flat_dependencies(args.file, args.path)
    elif args.simple:
        imports, delay_imports, error = get_imports(args.file)
        if error:
            print(f"Error: {error}")
            sys.exit(1)
        
        print(f"\nDirect dependencies of {os.path.basename(args.file)}:\n")
        if imports:
            for dll in sorted(imports.keys()):
                print(f"  {dll}")
        if delay_imports:
            print("\nDelay-loaded:")
            for dll in sorted(delay_imports.keys()):
                print(f"  {dll}")
    else:
        print_imports(args.file, show_functions=True)


if __name__ == "__main__":
    main()