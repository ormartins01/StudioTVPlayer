﻿using System;
using System.Collections.ObjectModel;
using System.Linq;
using StudioTVPlayer.Helpers;
using StudioTVPlayer.Model;
using StudioTVPlayer.Providers;

namespace StudioTVPlayer.ViewModel.Configuration
{
    public class WatchedFoldersViewModel : ModifyableViewModelBase
    {
        private readonly MahApps.Metro.Controls.Dialogs.IDialogCoordinator _dialogCoordinator = MahApps.Metro.Controls.Dialogs.DialogCoordinator.Instance;

        public UiCommand AddWatchedFolderCommand { get; }

        public ObservableCollection<WatchedFolderViewModel> WatchedFolders { get; }

        public WatchedFoldersViewModel()
        {
            WatchedFolders = new ObservableCollection<WatchedFolderViewModel>(GlobalApplicationData.Current.Configuration.WatchedFolders.Select(f =>
            {
                var vm = new WatchedFolderViewModel(f);
                vm.PropertyChanged += WatchedFolder_PropertyChanged;
                vm.RemoveRequested += WatchedFolder_RemoveRequested;
                return vm;
            }));
            AddWatchedFolderCommand = new UiCommand(AddWatchedFolder);
        }

        private void WatchedFolder_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(IsModified))
                IsModified = true;
        }

        private void AddWatchedFolder(object obj)
        {
            string path = Environment.GetFolderPath(Environment.SpecialFolder.MyComputer);
            if (FolderHelper.Browse(ref path, "Select path for new watched folder"))
            {
                var name = path.Split(new[] { System.IO.Path.DirectorySeparatorChar }, StringSplitOptions.RemoveEmptyEntries).LastOrDefault() ?? "New watched folder";
                var vm = new WatchedFolderViewModel(new WatchedFolder() { Path = path, Name = name });
                vm.RemoveRequested += WatchedFolder_RemoveRequested;
                WatchedFolders.Add(vm);
                IsModified = true;
            }
        }

        private async void WatchedFolder_RemoveRequested(object sender, EventArgs e)
        {
            var folder = sender as WatchedFolderViewModel ?? throw new ArgumentException(nameof(sender));
            if (await _dialogCoordinator.ShowMessageAsync(MainViewModel.Instance, "Confirmation", $"Really remove watched folder \"{folder.Name}\"?", MahApps.Metro.Controls.Dialogs.MessageDialogStyle.AffirmativeAndNegative) != MahApps.Metro.Controls.Dialogs.MessageDialogResult.Affirmative)
                return;
            WatchedFolders.Remove(folder);
            IsModified = true;
        }

        public override void Apply()
        {
            GlobalApplicationData.Current.Configuration.WatchedFolders = WatchedFolders.Select(f => {
                f.Apply();
                return f.WatchedFolder;
            }).ToList();
            IsModified = false;
        }

        public override bool IsValid()
        {
            return WatchedFolders.All(f => f.IsValid());
        }
    }
}