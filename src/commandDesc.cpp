#include <commandDesc.hpp>
#include <commands.hpp>
#include <cb.hpp>
#include <pipe.hpp>
#include <rp.hpp>
#include <util/util.hpp>
#include <vk/enumString.hpp>

namespace vil {

// CommandDesc
/*
void fillIn(CommandDesc& ret, const Command* siblings, const Command& cmd) {
	ret.command = cmd.nameDesc();
	ret.arguments = cmd.argumentsDesc();

	bool before = true;
	auto sibling = siblings;
	while(sibling) {
		if(sibling == &cmd) {
			before = false;
		}

		if(sibling->nameDesc() == ret.command) {
			++ret.count;

			if(before) {
				++ret.id;
			}
		}

		sibling = sibling->next;
	}

	dlg_assertm(!before, "Inconsistent sibling/cmd (broken command hierarchy)");
}

std::vector<CommandDesc> CommandDesc::get(const Command& root, span<const Command*> hierarchy) {
	std::vector<CommandDesc> ret;

	auto* siblingList = &root;
	for(auto* lvl : hierarchy) {
		dlg_assert(siblingList);
		fillIn(ret.emplace_back(), siblingList, *lvl);
		siblingList = lvl->children();
	}

	return ret;
}

std::vector<Command*> CommandDesc::findHierarchy(Command* root, span<const CommandDesc> desc) {
	if(desc.empty() || !root) {
		return {};
	}

	struct Candidate {
		Command* command {};
		float score {};

		bool operator<(const Candidate& other) const {
			return score < other.score;
		}
	};

	// TODO: when we can't find an exact match, we probably want to return
	// the nearest parent we could find (maybe require an even higher
	// threshold though since jumping to a false parent sucks).
	// Maybe control that behavior via an external argument
	auto findCandidates = [](Command* cmd, const CommandDesc& desc) -> std::vector<Candidate> {
		std::vector<Candidate> candidates;
		while(cmd) {
			if(cmd->nameDesc() == desc.command) {
				candidates.push_back({cmd, 0.f});
			}

			cmd = cmd->next;
		}

		for(auto c = 0u; c < candidates.size(); ++c) {
			auto& cand = candidates[c];

			auto args = cand.command->argumentsDesc();
			auto maxArgs = std::max(args.size(), desc.arguments.size());
			if(maxArgs > 0) {
				u32 numSame = 0u;
				for(auto i = 0u; i < std::min(args.size(), desc.arguments.size()); ++i) {
					if(args[i] == desc.arguments[i]) {
						++numSame;
					}
				}

				cand.score = float(numSame) / maxArgs;
			} else {
				cand.score = 1.0;
			}

			// weigh by distance
			// TODO: we can do better!
			// This is very sensitive to just erasing large chunks of data.
			// We can use desc.count to get a more precise score.
			float relDesc = float(desc.id) / desc.count;
			float relCand = float(c) / candidates.size();

			cand.score /= 1 + std::abs(5 * (relDesc - relCand));
		}

		// sort them in ascending order
		std::sort(candidates.begin(), candidates.end());

		// TODO: better filter
		auto threshold = 0.29f;
		auto cmp = [](float threshold, const auto& cand) {
			return threshold <= cand.score;
		};
		auto it = std::upper_bound(candidates.begin(), candidates.end(), threshold, cmp);
		candidates.erase(candidates.begin(), it);

		return candidates;
	};

	std::vector<Command*> ret;
	std::vector<std::vector<Candidate>> levels;
	levels.push_back(findCandidates(root, desc[0]));

	while(true) {
		if(levels.back().empty()) {
			if(ret.empty()) {
				// didn't find a matching command
				return ret;
			}

			levels.pop_back();
			ret.pop_back();
			continue;
		}

		// get the best parent candidate
		auto cand = levels.back().back();
		ret.push_back(cand.command);
		levels.back().pop_back();

		// if we are in the last level: good, just return the best
		// candidate we have found
		if(ret.size() == desc.size()) {
			return ret;
		}

		// otherwise: We must have a parent command.
		// Find all children candidates, and push them to the stack,
		// go one level deeper
		auto cands = findCandidates(cand.command->children(), desc[ret.size()]);
		levels.push_back(cands);
	}

	// dlg_error("unreachable");
}

Command* CommandDesc::find(Command* root, span<const CommandDesc> desc) {
	auto chain = findHierarchy(root, desc);
	return chain.empty() ? nullptr : chain.back();
}
*/

void processType(CommandBufferDesc& desc, Command::Type type) {
	switch(type) {
		case Command::Type::draw:
			++desc.drawCommands;
			break;
		case Command::Type::dispatch:
			++desc.dispatchCommands;
			break;
		case Command::Type::sync:
			++desc.syncCommands;
			break;
		case Command::Type::transfer:
			++desc.transferCommands;
			break;
		case Command::Type::query:
			++desc.queryCommands;
			break;
		default:
			break;
	}
}

// CommandBufferDesc CommandBufferDesc::get(const Command* cmd) {
CommandBufferDesc CommandBufferDesc::getAnnotate(Command* cmd) {
	CommandBufferDesc ret;
	ret.name = "root";

	// TODO: should really use allocator
	std::unordered_map<std::string, u32> ids;

	while(cmd) {
		if(auto children = cmd->children()) {
			auto child = CommandBufferDesc::getAnnotate(children);
			child.name = cmd->nameDesc();
			ret.children.push_back(std::move(child));

			// TODO: kinda hacky, should find general mechanism
			if(auto rpc = dynamic_cast<BeginRenderPassCmd*>(cmd); rpc) {
				dlg_assert(rpc->rp);

				for(auto& attachment : rpc->rp->desc->attachments) {
					child.params.push_back(vk::name(attachment.format));
				}
			}
		}

		processType(ret, cmd->type());

		++ret.totalCommands;
		cmd->relID = ids[cmd->nameDesc()]++;
		cmd = cmd->next;
	}

	return ret;
}

float match(const CommandBufferDesc& a, const CommandBufferDesc& b) {
	// compare children
	auto bcit = b.children.begin();
	float childMatchSum = 0u;

	for(const auto& ac : a.children) {
		// NOTE: when matching children we punish different orders
		// *extremely* harshly, namely: (A, B) is considered 0% similar
		// to (B, A). Intuitively, this seems ok for command buffer
		// sections but it might be a problem in some cases; improve
		// when need arises.
		// NOTE: we only compare for exactly same sections here.
		// We could also handle the case where labels e.g. include recording-
		// specific information. Could filter out numbers or do a lexical
		// distance check or something. Revisit if need ever arises.
		for(auto it = bcit; it != b.children.end(); ++it) {
			if(it->name == ac.name && std::equal(it->params.begin(), it->params.end(),
					ac.params.begin(), ac.params.end())) {
				// NOTE: we could weigh children with more total commands
				// more, via it->totalCommands. Probably a good idea
				// NOTE: when match is low, could look forward and choose
				// child with almost full match, indicating a skip.
				childMatchSum += match(*it, ac);
				bcit = it + 1;
				break;
			}
		}
	}

	auto maxChildren = std::max(a.children.size(), b.children.size());

	// compare own commands
	float weightSum = 0.f;
	float diffSum = 0.f;

	auto addMatch = [&](u32 dst, u32 src) {
		diffSum += std::abs(float(dst) - float(src));
		weightSum += std::max(dst, src);
	};

	addMatch(a.dispatchCommands, b.dispatchCommands);
	addMatch(a.drawCommands, b.drawCommands);
	addMatch(a.transferCommands, b.transferCommands);
	addMatch(a.syncCommands, b.syncCommands);
	addMatch(a.queryCommands, b.queryCommands);

	// When there are no commands in either, we match 100%
	float ownMatch = weightSum > 0.0 ? 1.f - diffSum / weightSum : 1.f;

	// NOTE: kinda simplistic formula, can surely be improved.
	// We might want to value large setions that are similar a lot
	// more since that is a huge indicator that command buffers come from
	// the same source, even if whole sections are missing in either of them.
	// NOTE: we currently value child sections a lot since
	// we are much more interested in the *structure* of a record than
	// the actual commands (since we don't correctly match them anyways,
	// only the numbers). Should be changed when doing better per-command
	// matching
	return (ownMatch + childMatchSum) / (1 + maxChildren);
}

// WIP
/*
struct Match {
	std::vector<std::pair<unsigned, unsigned>> map;
	std::vector<Match> children;
};

bool matchCommand(const Command& a, const Command& b);

Match matchRecord(const Command* a, const Command* b) {
	// notes:
	// - https://en.wikipedia.org/wiki/Hirschberg%27s_algorithm
	// - bundle together multiple consecutive dispatch/draw commands
	//   as long as state isn't changed in between I guess it does not
	//   matter too much?
	//   Hm maybe it does, when we have a draw command selected.
	//   Usually, programs don't use a high number of consecutive
	//   dispatch calls.
	//   Maybe rely on our hierachy again and only match for the children
	//   (i.e. render passes) that are relevant (i.e. in selected hierachy)?
	//   The n^2 number of comparisons might still be a problem, we must
	//   expect >1000 draw calls in a render pass.
	// nevm, likely not scalable enough. All diff algorithms have
	// O(n^2) which does not work for ~ n >= 1000
}
*/

bool match(const vil::DescriptorSet::Binding& a, const vil::DescriptorSet::Binding& b,
		const DescriptorSetLayout::Binding& layout) {
	if(!a.valid || !b.valid) {
		return a.valid == b.valid;
	}

	// TODO: if samplers or image/buffers views are different we could
	// check them for semantic equality as well. But who would do something
	// as terrible as create multiple equal samplers/imageView? /s

	auto dsCat = category(layout.descriptorType);
	if(dsCat == DescriptorCategory::image) {

		if(needsSampler(layout.descriptorType) &&
				!layout.immutableSamplers.get() &&
				a.imageInfo.sampler != b.imageInfo.sampler) {
			return false;
		}

		if(needsImageView(layout.descriptorType) &&
				a.imageInfo.imageView != b.imageInfo.imageView) {
			return false;
		}

		// NOTE: consider image layout? not too relevant I guess

		return true;
	} else if(dsCat == DescriptorCategory::bufferView) {
		return a.bufferView == b.bufferView;
	} else if(dsCat == DescriptorCategory::buffer) {
		// NOTE: consider offset? not too relevant I guess
		return a.bufferInfo.buffer == b.bufferInfo.buffer &&
			a.bufferInfo.range == b.bufferInfo.range;
	}

	dlg_error("unreachable! bogus descriptor type");
	return false;
}

FindResult find(const Command* root, span<const Command*> dst,
		const CommandDescriptorState& dsState, float threshold) {
	dlg_assert_or(!dst.empty(), return {});
	dlg_assert(root);

	std::vector<const Command*> bestCmds;
	float bestMatch = threshold;

	for(auto it = root; it; it = it->next) {
		auto m = it->match(*dst[0]);
		if(m > 0.f && m > bestMatch) {
			std::vector<const Command*> restCmds;

			if(dst.size() > 1) {
				dlg_assert(it->children());
				auto newThresh = bestMatch / m;
				auto restResult = find(it->children(), dst.subspan(1), dsState, newThresh);
				if(restResult.hierachy.empty()) {
					continue;
				}

				restCmds = std::move(restResult.hierachy);
				m *= restResult.match;
			} else if(!dsState.descriptors.empty()) {
				// match descriptors, if any
				span<const BoundDescriptorSet> bound;
				if(auto* cmd = dynamic_cast<const DrawCmdBase*>(dst[0])) {
					dlg_assert_or(cmd->state.pipe, continue);
					bound = cmd->state.descriptorSets;
					bound = bound.first(cmd->state.pipe->layout->descriptors.size());
				} else if(auto* cmd = dynamic_cast<const DispatchCmdBase*>(dst[0])) {
					dlg_assert_or(cmd->state.pipe, continue);
					bound = cmd->state.descriptorSets;
					bound = bound.first(cmd->state.pipe->layout->descriptors.size());
				} else {
					// unreachable
					dlg_error("Unexepcted command type; does not have descriptors");
					continue;
				}

				dlg_assertm_or(bound.size() == dsState.descriptors.size(),
					continue, "Descriptor count does not match");

				// compare descriptor state
				unsigned count {};
				unsigned match {};

				for(auto s = 0u; s < bound.size(); ++s) {
					dlg_assert(bound[s].ds);
					count += bound[s].ds->layout->totalNumBindings;

					// TODO: consider dynamic offsets?
					// TODO: additional bonus matching points when the *same*
					//   ds is used?
					if(bound[s].ds == dsState.descriptors[s].ds) {
						// fasth path: full match since same descriptorSet
						match += bound[s].ds->layout->totalNumBindings;
						continue;
					}

					if(dsState.descriptors[s].ds) {
						dlg_assert_or(bound[s].ds->bindings.size() ==
							dsState.descriptors[s].ds->bindings.size(), continue);
					} else {
						dlg_assert_or(bound[s].ds->bindings.size() ==
							dsState.descriptors[s].state.size(), continue);
					}

					// iterate over bindings
					for(auto b = 0u; b < bound[s].ds->bindings.size(); ++b) {
						auto& binding1 = bound[s].ds->bindings[b];
						auto& layout = bound[s].ds->layout->bindings[b];

						std::vector<vil::DescriptorSet::Binding>* binding0 {};
						if(dsState.descriptors[s].ds) {
							dlg_assert_or(bound[s].ds->bindings.size() ==
								dsState.descriptors[s].ds->bindings.size(), continue);
							binding1 = dsState.descriptors[s].ds->bindings[b];
						} else {
							dlg_assert_or(bound[s].ds->bindings.size() ==
								dsState.descriptors[s].state.size(), continue);
							binding1 = dsState.descriptors[s].state[b];
						}

						dlg_assert_or(binding1.size() == binding0->size(), continue);

						for(auto e = 0u; e < binding1.size(); ++e) {
							auto& elem0 = (*binding0)[e];
							auto& elem1 = binding1[e];

							if(vil::match(elem0, elem1, layout)) {
								++match;
							}
						}
					}
				}

				float dsMatch = float(match) / count;
				m *= dsMatch;
			}

			if(m > bestMatch) {
				bestCmds.clear();
				bestCmds.push_back(it);
				bestCmds.insert(bestCmds.end(), restCmds.begin(), restCmds.end());
				bestMatch = m;
			}
		}
	}

	return {bestCmds, bestMatch};
}

} // namespace vil
