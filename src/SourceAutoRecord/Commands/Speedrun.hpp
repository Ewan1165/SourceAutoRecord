#pragma once
#include "Modules/Console.hpp"
#include "Modules/Engine.hpp"

#include "Features/Speedrun.hpp"

#include "Command.hpp"

CON_COMMAND(sar_speedrun_result, "Prints result of speedrun.\n")
{
    auto session = Speedrun::timer->GetSession();
    auto total = Speedrun::timer->GetTotal();
    auto ipt = Speedrun::timer->GetIntervalPerTick();

    if (Speedrun::timer->IsRunning()) {
        console->PrintActive("Session: %s (%i)\n", Speedrun::Timer::Format(session * ipt).c_str(), session);
        console->PrintActive("Total:   %s (%i)\n", Speedrun::Timer::Format(total * ipt).c_str(), total);
    } else {
        auto splits = (args.ArgC() == 2 && !std::strcmp(args[1], "pb"))
            ? Speedrun::timer->GetPersonalBest()->splits
            : Speedrun::timer->GetResult()->splits;

        if (splits.size() == 0) {
            console->Warning("No result to show!\n");
        }

        auto segments = 0;
        for (auto& split : splits) {
            auto completedIn = split.GetTotal();
            console->Print("%s in %s (%i)\n", split.map, Speedrun::Timer::Format(completedIn * ipt).c_str(), completedIn);
            for (const auto& seg : split.segments) {
                console->Msg("-> %s (%i)\n", Speedrun::Timer::Format(seg.session * ipt).c_str(), seg.session);
                ++segments;
            }
        }

        console->Print("Splits: %i\n", segments);
        console->Print("Total:  %s (%i)\n", Speedrun::Timer::Format(total * ipt).c_str(), total);
    }
}

CON_COMMAND(sar_speedrun_export, "Saves speedrun result to a csv file.\n")
{
    if (args.ArgC() != 2) {
        console->Print("sar_speedrun_export [file_name] : Saves speedrun result to a csv file.\n");
        return;
    }

    auto filePath = std::string(Engine::GetGameDirectory()) + std::string("/") + std::string(args[1]);
    if (filePath.substr(filePath.length() - 4, 4) != ".csv")
        filePath += ".csv";

    if (Speedrun::timer->ExportResult(filePath)) {
        console->Print("Exported result!\n");
    } else {
        console->Warning("Failed to export result!\n");
    }
}

CON_COMMAND(sar_speedrun_export_pb, "Saves speedrun personal best to a csv file.\n")
{
    if (args.ArgC() != 2) {
        console->Print("sar_speedrun_export_pb [file_name] : Saves speedrun personal best to a csv file.\n");
        return;
    }

    auto filePath = std::string(Engine::GetGameDirectory()) + std::string("/") + std::string(args[1]);
    if (filePath.substr(filePath.length() - 4, 4) != ".csv")
        filePath += ".csv";

    if (Speedrun::timer->ExportPersonalBest(filePath)) {
        console->Print("Exported personal best!\n");
    } else {
        console->Warning("Failed to export personal best!\n");
    }
}

CON_COMMAND_AUTOCOMPLETEFILE(sar_speedrun_import, "", 0, 0, csv)
{
    if (args.ArgC() != 2) {
        console->Print("sar_speedrun_import [file_name] : Imports speedrun data file.\n");
        return;
    }

    auto filePath = std::string(Engine::GetGameDirectory()) + std::string("/") + std::string(args[1]);
    if (filePath.substr(filePath.length() - 4, 4) != ".csv")
        filePath += ".csv";

    if (Speedrun::timer->ImportPersonalBest(filePath)) {
        console->Print("Imported personal best!\n");
    } else {
        console->Warning("Failed to import file!\n");
    }
}

CON_COMMAND(sar_speedrun_rules, "Prints loaded rules which the timer will follow.\n")
{
    auto rules = Speedrun::timer->GetRules();
    if (rules->size() == 0) {
        console->Print("No rules loaded!\n");
        return;
    }

    for (const auto& rule : *rules) {
        console->Print("%s\n", rule.map);
        console->Print("    -> Event: %s\n", rule.activator);
        console->Print("    -> Type:  %s\n", (rule.action == Speedrun::TimerAction::Start) ? "Start" : "Stop");
    }
}
