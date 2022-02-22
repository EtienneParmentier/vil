#include <command/builder.hpp>
#include <command/commands.hpp>
#include <command/alloc.hpp>

#ifdef VIL_COMMAND_CALLSTACKS
	#include <util/callstack.hpp>
#endif // VIL_COMMAND_CALLSTACKS

namespace vil {

// util
void doReset(RecordBuilder& builder) {
	builder.record_->commands = &construct<RootCommand>(*builder.record_);
	builder.section_ = &construct<RecordBuilder::Section>(*builder.record_);
	builder.section_->cmd = builder.record_->commands;
	builder.lastCommand_ = nullptr;
}

// RecordBuilder
RecordBuilder::RecordBuilder(Device& dev) {
	reset(dev);
}

void RecordBuilder::reset(Device& dev) {
	record_ = IntrusivePtr<CommandRecord>(new CommandRecord(manualTag, dev));
	doReset(*this);
}

RecordBuilder::RecordBuilder(CommandBuffer& cb) {
	reset(cb);
}

void RecordBuilder::reset(CommandBuffer& cb) {
	record_ = IntrusivePtr<CommandRecord>(new CommandRecord(cb));
	doReset(*this);
}

void RecordBuilder::appendParent(ParentCommand& cmd) {
	dlg_assert(section_);

	dlg_assert(!!section_->lastParentChild == !!section_->cmd->firstChildParent_);
	if(section_->lastParentChild) {
		section_->lastParentChild->nextParent_ = &cmd;
	} else {
		section_->cmd->firstChildParent_ = &cmd;
	}

	section_->lastParentChild = &cmd;
	++section_->cmd->stats_.numChildSections;
}

void RecordBuilder::beginSection(SectionCommand& cmd) {
	appendParent(cmd);

	if(section_->next) {
		// re-use a previously allocated section that isn't in use anymore
		dlg_assert(!section_->next->cmd);
		dlg_assert(section_->next->parent == section_);
		section_ = section_->next;
		section_->cmd = nullptr;
		section_->pop = false;
		section_->lastParentChild = nullptr;
	} else {
		auto nextSection = &construct<Section>(*record_);
		nextSection->parent = section_;
		section_->next = nextSection;
		section_ = nextSection;
	}

	section_->cmd = &cmd;
	lastCommand_ = nullptr; // will be set on first addCmd
}

void RecordBuilder::endSection(Command* cmd) {
	if(!section_->parent) {
		// We reached the root section.
		// Debug utils commands can span multiple command buffers, they
		// are only queue-local.
		// See docs/debug-utils-label-nesting.md
		dlg_assert(dynamic_cast<EndDebugUtilsLabelCmd*>(cmd));
		++record_->numPopLabels;
		return;
	}

	lastCommand_ = section_->cmd;
	dlg_assert(!section_->pop); // we shouldn't be able to land here

	// reset it for future use
	section_->cmd = nullptr;
	section_->pop = false;

	// Don't unset section_->next, we can re-use the allocation
	// later on. We unset 'cmd' above to signal its unused (as debug check)
	section_ = section_->parent;

	// We pop the label sections here that were previously ended by
	// the application but not in the same nesting level they were created.
	while(section_->parent && section_->pop) {
		dlg_assert(dynamic_cast<BeginDebugUtilsLabelCmd*>(section_->cmd));
		lastCommand_ = section_->cmd;

		// reset it for future use
		section_->cmd = nullptr;
		section_->pop = false;

		section_ = section_->parent;
	}
}

void RecordBuilder::append(Command& cmd) {
	dlg_assert(record_);
	dlg_assert(section_);

#ifdef VIL_COMMAND_CALLSTACKS
	// TODO: does not really belong here. should be atomic then at least
	if(record_->dev->captureCmdStack) {
		cmd.stackTrace = &construct<backward::StackTrace>(*record_);
		cmd.stackTrace->load_here(32u);
	}
#endif // VIL_COMMAND_CALLSTACKS

	// add to stats
	++section_->cmd->stats_.numTotalCommands;
	switch(cmd.type()) {
		case CommandType::draw:
			++section_->cmd->stats_.numDraws;
			break;
		case CommandType::dispatch:
			++section_->cmd->stats_.numDispatches;
			break;
		case CommandType::traceRays:
			++section_->cmd->stats_.numRayTraces;
			break;
		case CommandType::sync:
			++section_->cmd->stats_.numSyncCommands;
			break;
		case CommandType::transfer:
			++section_->cmd->stats_.numTransfers;
			break;
		default:
			break;
	}

	// append
	if(lastCommand_) {
		dlg_assert(record_->commands);
		lastCommand_->next = &cmd;
	} else {
		dlg_assert(record_->commands);
		dlg_assert(section_->cmd);
		dlg_assert(!section_->cmd->children_);
		section_->cmd->children_ = &cmd;
	}

	lastCommand_ = &cmd;
}

} // namespace vil
