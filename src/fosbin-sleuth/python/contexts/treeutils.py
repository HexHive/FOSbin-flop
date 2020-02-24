import contexts.FBDecisionTree as FBDtree


# def diff_two_guess_sets(guesses0, guesses1):
#     funcs0 = dict()
#     funcs1 = dict()
#     for fd, idx in guesses0.items():
#         funcs0[fd.name] = idx
#     for fd, idx in guesses1.items():
#         funcs1[fd.name] = idx
#
#     missing_in_0 = set()
#     missing_in_1 = set()
#     differing = set()
#     for name, idx in funcs0.items():
#         if name not in funcs1:
#             missing_in_1.add(name)
#         elif idx != funcs1[name]:
#             differing.add(name)
#
#     for name, idx in funcs1.items():
#         if name not in funcs0:
#             missing_in_0.add(name)
#         elif idx != funcs0[name]:
#             differing.add(name)
#
#     return missing_in_0, missing_in_1, differing


# def histogram_data(tree):
#     sizes = list()
#     for ec in tree.get_all_equiv_classes():
#         sizes.append(len(ec))
#
#     return sizes


# def bin_data(tree, n_bins=10):
#     hist_data = histogram_data(tree)
#     bins = list()
#
#     for size in hist_data:
#         s = size
#         if s > n_bins:
#             s = n_bins
#
#         bins.append(s)
#
#     return bins


def get_evaluation(tree, guesses, equivalence_map=None):
    func_names = set()
    true_pos = set()
    true_neg = set()
    incorrect = set()
    for fd in tree.get_func_descs():
        func_names.add(fd.name)

    for func_desc, idx in guesses.items():
        if "ifunc" in func_desc.name:
            continue

        equiv_class = tree.get_equiv_classes(idx)
        if equiv_class is not None:

            found = False
            for ec in equiv_class:
                if "ifunc" in ec.name:
                    continue

                ec_name = ec.name
                func_desc_name = func_desc.name

                if equivalence_map is not None:
                    if ec_name in equivalence_map:
                        ec_name = equivalence_map[ec_name]
                    if func_desc_name in equivalence_map:
                        func_desc_name = equivalence_map[func_desc_name]

                if ec_name == func_desc_name:
                    found = True
                    break

            if found:
                true_pos.add(func_desc.name)
            else:
                incorrect.add(func_desc.name)
        else:
            if func_desc.name in func_names:
                incorrect.add(func_desc.name)
            else:
                true_neg.add(func_desc.name)

    return true_pos, true_neg, incorrect


def get_tree_coverage(dtree, target_func_desc_name):
    tree_path = get_tree_path(dtree, target_func_desc_name)
    path_coverages = list()
    for node in tree_path:
        if isinstance(node, FBDtree.FBDecisionTreeInteriorNode):
            for (func_desc, coverage) in node.get_coverage().items():
                if func_desc.name == target_func_desc_name:
                    path_coverages.append(coverage)
    return path_coverages


def get_individual_tree_coverage(dtree):
    coverages = dict()
    for func_desc in dtree.get_func_descs():
        coverages[func_desc] = get_tree_coverage(dtree, func_desc.name)
    return coverages


def get_full_tree_coverage(dtree):
    executed_instructions = set()
    reachable_instruction_count = dict()

    total_reachable_instructions = 0
    for node in dtree.get_all_interior_nodes():
        for func_desc, coverage_data in node.get_coverage().items():
            for (instructions, instruction_count) in coverage_data:
                start_addr = instructions[0]
                if start_addr not in reachable_instruction_count:
                    total_reachable_instructions += instruction_count
                    reachable_instruction_count[start_addr] = instruction_count
                for inst in instructions:
                    executed_instructions.add(inst)

    return len(executed_instructions) / total_reachable_instructions


def get_tree_path(tree, func_name):
    path = list()
    path.append(tree.root)
    if _dfs_tree(func_name, path):
        return path
    path.pop()
    return path


def _dfs_tree(func_name, path):
    if path[-1].is_leaf():
        for ec in path[-1].get_equivalence_class():
            if ec.name == func_name:
                return True
        return False
    else:
        path.append(path[-1].get_left_child())
        if _dfs_tree(func_name, path):
            return True
        path.pop()
        path.append(path[-1].get_right_child())
        if _dfs_tree(func_name, path):
            return True
        path.pop()
        return False


def compute_path_quality(dtree, func_name):
    path = get_tree_path(dtree, func_name)
    executed_instructions = list()
    total_instruction_count = list()
    for node in path:
        if isinstance(node, FBDtree.FBDecisionTreeInteriorNode):
            for (func_desc, coverage) in node.get_coverage().items():
                if func_desc.name == func_name:
                    for (instructions, total_instructions) in coverage:
                        executed_instructions.append(len(instructions))
                        total_instruction_count.append(total_instructions)
                    break

    if sum(total_instruction_count) > 0:
        return sum(executed_instructions) / sum(total_instruction_count)
    else:
        return 0

# def get_func_indices(tree):
#     tree_funcs = dict()
#     for idx in range(0, tree.size()):
#         if tree._is_leaf(idx):
#             for ec in tree.get_equiv_classes(idx):
#                 tree_funcs[ec.name] = idx
#
#     return tree_funcs


# def output_incorrect(tree, guesses):
#     true_pos, true_neg, incorrect = get_evaluation(tree, guesses)
#     tree_funcs = dict()
#     for idx in range(0, tree.size()):
#         if tree._is_leaf(idx):
#             equiv_classes = tree.get_equiv_classes(idx)
#             for ec in equiv_classes:
#                 tree_funcs[ec.name] = idx
#
#     for fd, guess in guesses.items():
#         if fd.name in incorrect:
#             correct = list()
#             if fd.name in tree_funcs:
#                 equiv_classes = tree.get_equiv_classes(tree_funcs[fd.name])
#                 for ec in equiv_classes:
#                     correct.append(ec.name)
#                 correct.sort()
#             else:
#                 correct.append("UNKNOWN")
#
#             equiv_classes = tree.get_equiv_classes(guess)
#             if equiv_classes is not None:
#                 names = list()
#                 for ec in equiv_classes:
#                     names.append(ec.name)
#                 names.sort()
#                 print("{}: {} <--> {}".format(fd.name, " ".join(names), " ".join(correct)))
#             else:
#                 print("{}: UNKNOWN <--> {}".format(fd.name, " ".join(correct)))
#             print()
